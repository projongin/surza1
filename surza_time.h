#pragma once

#include <stdint.h>
#include "net_messages.h"

//допускается вызов как в прерывании, так и в основной программе 
surza_time_t  get_time();

