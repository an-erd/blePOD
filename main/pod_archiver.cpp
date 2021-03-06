#include <string.h>
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "pod_main.h"

static const char* TAG = "POD_ARCHIVER";

// data buffers to keep in RAM
//   => CONFIG_SDCARD_NUM_BUFFERS=4 and CONFIG_SDCARD_BUFFER_SIZE=65536
static buffer_element_t buffers[CONFIG_SDCARD_NUM_BUFFERS][CONFIG_SDCARD_BUFFER_SIZE] EXT_RAM_ATTR;
static uint8_t          current_buffer;                             // buffer to write next elements to
static uint32_t         used_in_buffer[CONFIG_SDCARD_NUM_BUFFERS];  // position in resp. buffer
static uint8_t          next_buffer_to_write;                       // next buffer to write to
static int              s_ref_file_nr = -2;
static bool             new_file = true;

EventGroupHandle_t      pod_sd_evg;

// static  archiver buffer event group
#define BUFFER_BLEADV_BIT        (BIT0)
static EventGroupHandle_t buffer_evg;

// forward declaration
void pod_archiver_set_to_next_buffer();

static void clean_buffer(uint8_t num_buffer)
{
    memset(buffers[num_buffer], 0, sizeof(buffers[num_buffer])+1);
    used_in_buffer[num_buffer] = 0;
}

void pod_archiver_initialize()
{
    buffer_evg = xEventGroupCreate();
    for (int i = 0; i < CONFIG_SDCARD_NUM_BUFFERS; i++){
        clean_buffer(i);
    }
    current_buffer = 0;
    next_buffer_to_write = 0;
}

//TF card test begin
void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    ESP_LOGD(TAG, "listDir() > , Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        ESP_LOGW(TAG, "listDir(), Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        ESP_LOGW(TAG, "listDir(), Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            ESP_LOGD(TAG, "listDir(), DIR: %s", file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            ESP_LOGD(TAG, "listDir(), FILE: %s, SIZE: %u", file.name(), file.size());
        }
        file = root.openNextFile();
    }
    ESP_LOGD(TAG, "listDir() <");
}

void readFile(fs::FS &fs, const char * path) {
    ESP_LOGD(TAG, "readFile() >, Reading file: %s", path);

    File file = fs.open(path);
    if(!file){
        ESP_LOGW(TAG, "readFile(), Failed to open file for reading");
        return;
    }

    ESP_LOGD(TAG, "readFile(), Read from file: ");
    while(file.available()){
        int ch = file.read();
        ESP_LOGD(TAG, "readFile(): %c", ch);
    }
    ESP_LOGD(TAG, "readFile() <");
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    ESP_LOGD(TAG, "writeFile() >, Writing file: %s", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        ESP_LOGW(TAG, "writeFile(), Failed to open file for writing");
        return;
    }
    if(file.print(message)){
        ESP_LOGD(TAG, "writeFile() <, file written");
    } else {
        ESP_LOGE(TAG, "writeFile() <, write failed");
    }
}
//TF card test end


static int read_sd_card_file_refnr()
{
    // file handle and buffer to read
    FILE*   f;
    char    line[64];
    int     file_nr = -1;
    struct 	stat st;

    ESP_LOGD(TAG, "read_sd_card_file_refnr() >");

    if (stat("/sd/refnr.txt", &st) == 0) {
        f = fopen("/sd/refnr.txt", "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "read_sd_card_file_refnr(): Failed to open file for reading");
            return -1;
        }
	} else {
        f = fopen("/sd/refnr.txt", "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "read_sd_card_file_refnr(): Failed to open file for writing");
            return -1;
        }
        fprintf(f, "0\n");	// start new file with (file_nr=) 0
        fclose(f);

        ESP_LOGD(TAG, "read_sd_card_file_refnr(): New file refnr.txt written");

        f = fopen("/sd/refnr.txt", "r");
        if (f == NULL) {
            ESP_LOGE(TAG, "read_sd_card_file_refnr(): Failed to open file for reading (2)");
            return -1;
        }
    }

    fgets(line, sizeof(line), f);
    fclose(f);

    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGD(TAG, "read_sd_card_file_refnr(): Read from file: '%s'", line);
    sscanf(line, "%d", (int*)&file_nr);

    s_ref_file_nr = file_nr;

    ESP_LOGD(TAG, "read_sd_card_file_refnr() <, file_nr = %d", file_nr);

    return file_nr;
}

static int write_sd_card_file_refnr(int new_refnr)
{
    // file handle and buffer to read
    FILE*   f;
    // char    line[64];
    struct  stat st;

    ESP_LOGD(TAG, "write_sd_card_file_refnr() >, new_refnr = %d", new_refnr);
    if (stat("/sd/refnr.txt", &st) == 0) {
        ESP_LOGD(TAG, "write_sd_card_file_refnr(), stat = 0");
        f = fopen("/sd/refnr.txt", "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "write_sd_card_file_refnr() <, Failed to open file for writing");
            return -1;
        }
        fprintf(f, "%d\n", new_refnr);
        fclose(f);
        ESP_LOGD(TAG, "write_sd_card_file_refnr() <, File written");
    }

    return new_refnr;
}

int write_out_buffers(bool write_only_completed)
{

    // file handle and buffer to read
    FILE*   f;
    char    line[64];
    int     file_nr;
    char    strftime_buf[64];

    ESP_LOGD(TAG, "write_out_buffers() >");

    // Read the last file number from file. If it does not exist, create a new and start with "0".
    // If it exists, read the value and increase by 1.
    file_nr = read_sd_card_file_refnr();
    if(file_nr < 0)
        ESP_LOGE(TAG, "write_out_buffers(): Ref. file could not be read or created - ABORT");

    // generate a new reference number if requested and store in file for upcoming use
    if(new_file){
        file_nr++;
        new_file = false;
        write_sd_card_file_refnr(file_nr);
    }

    // generate new file name
    sprintf(line, CONFIG_SDCARD_FILE_NAME, file_nr);
    ESP_LOGD(TAG, "write_out_buffers(): generated file name '%s'", line);

    f = fopen(line, "a");
    if(f == NULL) {
        f = fopen(line, "w");
        if (f == NULL) {
            ESP_LOGE(TAG, "write_out_buffers(): Failed to open file for writing");
            return 0;
        }
        ESP_LOGD(TAG, "write_out_buffers(): Created new file for write");
    } else {
        ESP_LOGD(TAG, "write_out_buffers(): Opening file for append");
    }

	if(!write_only_completed){
		// switch to next buffer to ensure no conflict of new incoming data
		current_buffer = (current_buffer + 1) % CONFIG_SDCARD_NUM_BUFFERS;
		used_in_buffer[current_buffer] = 0;		// just to be sure
	}
	while( next_buffer_to_write != current_buffer ){
		// write buffer next_buffer_to_write
		// ESP_LOGD(TAG, "write_out_buffers(): write buffers[%u][0..%u]", next_buffer_to_write, used_in_buffer[next_buffer_to_write]);

		// for(uint32_t i = 0; i < used_in_buffer[next_buffer_to_write]; i++){
		// 	// check for timestamp:       strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        //     // TODO
        //     if(buffers[next_buffer_to_write][i].timeinfo.tm_year != 0){
		// 	    strftime(strftime_buf, sizeof(strftime_buf), "%c", &buffers[next_buffer_to_write][i].timeinfo);
		// 	    fprintf(f, "%u:%u,%s,%u,%u,%u\r\n", next_buffer_to_write, i, strftime_buf,
        //             buffers[next_buffer_to_write][i].cad, buffers[next_buffer_to_write][i].str, buffers[next_buffer_to_write][i].GCT);
        //         ESP_LOGD(TAG, "write_out_buffers(): Write: %u:%u,%s,%u,%u,%u\n", next_buffer_to_write, i, strftime_buf,
        //             buffers[next_buffer_to_write][i].cad, buffers[next_buffer_to_write][i].str, buffers[next_buffer_to_write][i].GCT);
        //     } else {
        //         fprintf(f, "%u:%u,,%u,%u,%u\r\n", next_buffer_to_write, i,
        //             buffers[next_buffer_to_write][i].cad, buffers[next_buffer_to_write][i].str, buffers[next_buffer_to_write][i].GCT);
        //         ESP_LOGD(TAG, "write_out_buffers(): Write: %u:%u,,,%u,%u,%u\n", next_buffer_to_write, i,
        //             buffers[next_buffer_to_write][i].cad, buffers[next_buffer_to_write][i].str, buffers[next_buffer_to_write][i].GCT);
        //     }
        // }
		clean_buffer(next_buffer_to_write);
		next_buffer_to_write = (next_buffer_to_write + 1) % CONFIG_SDCARD_NUM_BUFFERS;
	}

    ESP_LOGD(TAG, "write_out_buffers(): Closing file");
    fclose(f);

    ESP_LOGD(TAG, "write_out_buffers() <");

    return 1;
}

void pod_archiver_set_next_element()
{
    // int complete = xEventGroupWaitBits(dispod_sd_evg, POD_SD_WRITE_COMPLETED_BUFFER_EVT, pdFALSE, pdFALSE, 0) & POD_SD_WRITE_COMPLETED_BUFFER_EVT;
    // ESP_LOGD(TAG, "pod_archiver_set_next_element >: current_buffer %u, used in current buffer %u, size %u, complete %u",
    //     current_buffer, used_in_buffer[current_buffer], CONFIG_SDCARD_BUFFER_SIZE, complete);

    used_in_buffer[current_buffer]++;
    if(used_in_buffer[current_buffer] == CONFIG_SDCARD_BUFFER_SIZE){
		pod_archiver_set_to_next_buffer();
    }
	// buffers[current_buffer][used_in_buffer[current_buffer]].timeinfo.tm_year =  0;
	// buffers[current_buffer][used_in_buffer[current_buffer]].cad = 0;
	// buffers[current_buffer][used_in_buffer[current_buffer]].GCT = 0;
	// buffers[current_buffer][used_in_buffer[current_buffer]].str = 9;					// TODO check, wheter  this case happens?!

    // complete = xEventGroupWaitBits(dispod_sd_evg, DISPOD_SD_WRITE_COMPLETED_BUFFER_EVT, pdFALSE, pdFALSE, 0) & DISPOD_SD_WRITE_COMPLETED_BUFFER_EVT;
    // ESP_LOGD(TAG, "dispod_archiver_set_next_element <: current_buffer %u, used in current buffer %u, size %u, complete %u",
    //     current_buffer, used_in_buffer[current_buffer], CONFIG_SDCARD_BUFFER_SIZE, complete);
}

void pod_archiver_set_to_next_buffer()
{
    ESP_LOGD(TAG, "pod_archiver_set_to_next_buffer >: current_buffer %u, used in current buffer %u ", current_buffer, used_in_buffer[current_buffer]);
    if( used_in_buffer[current_buffer] != 0) {
        // set to next (free) buffer and write (all and maybe incomplete) buffer but current
        current_buffer = (current_buffer + 1) % CONFIG_SDCARD_NUM_BUFFERS;
		used_in_buffer[current_buffer] = 0;
        xEventGroupSetBits(pod_sd_evg, POD_SD_WRITE_COMPLETED_BUFFER_EVT);
    } else {
        // do nothing because current buffer is empty
		ESP_LOGD(TAG, "pod_archiver_set_to_next_buffer: current_buffer empty, do nothing");

    }
    ESP_LOGD(TAG, "pod_archiver_set_to_next_buffer <: current_buffer %u, used in current buffer %u ", current_buffer, used_in_buffer[current_buffer]);
}

void pod_archiver_check_full_element()
{
    EventBits_t uxBits;

	// uxBits = xEventGroupWaitBits(buffer_evg, BUFFER_RSC_BIT | BUFFER_CUSTOM_BIT, pdTRUE, pdTRUE, 0);
	// if( (uxBits & (BUFFER_RSC_BIT | BUFFER_CUSTOM_BIT) ) == (BUFFER_RSC_BIT | BUFFER_CUSTOM_BIT) ){
	// 	pod_archiver_set_next_element();
	// }
}

void dispod_archiver_add_bleadv_values(/* TODO */)
{
    // buffers[current_buffer][used_in_buffer[current_buffer]].GCT = new_GCT;
    // buffers[current_buffer][used_in_buffer[current_buffer]].str = new_str;
	// xEventGroupSetBits(buffer_evg, BUFFER_CUSTOM_BIT);

    pod_archiver_check_full_element();
}

void pod_archiver_add_time(tm timeinfo)
{
    char strftime_buf[64];

	buffers[current_buffer][used_in_buffer[current_buffer]].timeinfo = timeinfo;

    strftime(strftime_buf, sizeof(strftime_buf), "%c", &buffers[current_buffer][used_in_buffer[current_buffer]].timeinfo);
    ESP_LOGD(TAG, "pod_archiver_add_time(): TIME: %s", strftime_buf);
}

void pod_archiver_set_new_file()
{
    new_file = true;
}

void pod_archiver_task(void *pvParameters)
{
    ESP_LOGI(TAG, "pod_archiver_task: started");
    EventBits_t uxBits;
	bool		write_only_completed = true;

    for (;;)
    {
        uxBits = xEventGroupWaitBits(pod_sd_evg,
                POD_SD_WRITE_COMPLETED_BUFFER_EVT | POD_SD_WRITE_ALL_BUFFER_EVT | POD_SD_PROBE_EVT | POD_SD_GENERATE_TESTDATA_EVT,
                pdTRUE, pdFALSE, portMAX_DELAY);
        ESP_LOGD(TAG, "pod_archiver_task(): uxBits = %u", uxBits);
        if(uxBits & POD_SD_PROBE_EVT){
            xEventGroupClearBits(pod_sd_evg, POD_SD_PROBE_EVT);

            ESP_LOGD(TAG, "DISPOD_SD_PROBE_EVT: DISPOD_SD_PROBE_EVT");
            ESP_LOGI(TAG, "SD card info %d, card size kb %llu, total kb %llu used kb %llu, used %3.1f perc.",
                SD.cardType(), SD.cardSize()/1024, SD.totalBytes()/1024, SD.usedBytes()/1024, (SD.usedBytes() / (float) SD.totalBytes()));
            // TF card test
            // listDir(SD, "/", 0);
            // writeFile(SD, "/hello.txt", "Hello world");
            // readFile(SD, "/hello.txt");
            if(SD.cardType() != CARD_NONE){
                xEventGroupSetBits(pod_evg, POD_SD_AVAILABLE_BIT);
                pod_screen_status_update_sd(&pod_screen_status, SD_AVAILABLE);
                xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_SD_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            } else {
                xEventGroupClearBits(pod_evg, POD_SD_AVAILABLE_BIT);
                pod_screen_status_update_sd(&pod_screen_status, SD_NOT_AVAILABLE);
                xEventGroupSetBits(pod_evg, POD_DISPLAY_UPDATE_BIT);
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_SD_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            }
        }

        if((uxBits & POD_SD_WRITE_COMPLETED_BUFFER_EVT) == POD_SD_WRITE_COMPLETED_BUFFER_EVT){
			write_only_completed = !( (uxBits & POD_SD_WRITE_ALL_BUFFER_EVT) == POD_SD_WRITE_ALL_BUFFER_EVT);
            ESP_LOGD(TAG, "pod_archiver_task: POD_SD_WRITE_COMPLETED_BUFFER_EVT, write_only_completed=%d", write_only_completed);
            if( xEventGroupWaitBits(pod_evg, POD_SD_AVAILABLE_BIT, pdFALSE, pdFALSE, portMAX_DELAY) & POD_SD_AVAILABLE_BIT){
                write_out_buffers(write_only_completed);
            } else {
                ESP_LOGE(TAG, "pod_archiver_task: write out completed buffers but no SD mounted");
            }
        }

        if((uxBits & POD_SD_GENERATE_TESTDATA_EVT) == POD_SD_GENERATE_TESTDATA_EVT){
            ESP_LOGD(TAG, "dispod_archiver_task: POD_SD_GENERATE_TESTDATA_EVT start");
            for(int i=0; i < CONFIG_SDCARD_NUM_BUFFERS; i++){
                for(int j=0; j < CONFIG_SDCARD_BUFFER_SIZE; j++){
                    if(j%2){
                        // pod_archiver_add_RSCValues(180);
                    } else {
                        // pod_archiver_add_customValues(352,1);
                    }
                    if((i==(CONFIG_SDCARD_NUM_BUFFERS-1)) && j == (CONFIG_SDCARD_BUFFER_SIZE-4))
                        break;
                }
            }
            ESP_LOGD(TAG, "pod_archiver_task: POD_SD_GENERATE_TESTDATA_EVT done");
        }
    }
}
