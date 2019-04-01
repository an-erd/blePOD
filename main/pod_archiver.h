#ifndef __POD_ARCHIVER_H__
#define __POD_ARCHIVER_H__

#include "pod_main.h"

// parameters for data buffers
typedef struct {
	struct tm timeinfo;	// empty: tm_year=0
    // TODO which data in buffer?
} buffer_element_t;

void pod_archiver_initialize();
void pod_archiver_add_time(tm timeinfo);
void pod_archiver_set_new_file();    // set flag to open a new file and afterwards only append

void pod_archiver_task(void *pvParameters);

#endif // __POD_ARCHIVER_H__
