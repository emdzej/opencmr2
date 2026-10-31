/* Model viewer (port-plan step 3's check): --param car=<model> or --param stage=<name>. */
#pragma once
#include <stdbool.h>

bool viewer_init(const char *car, const char *stage);   /* one of them */
void viewer_frame(void);
