/** @file
 * Copyright (c) 2016-2018, 2021 Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
**/

#ifndef __BSA_ACS_PERIPHERALS_H__
#define __BSA_ACS_PERIPHERALS_H__



uint32_t os_d001_entry(uint32_t num_pe);
uint32_t os_d002_entry(uint32_t num_pe);
uint32_t os_d003_entry(uint32_t num_pe);
uint32_t os_d004_entry(uint32_t num_pe);


#define WIDTH_BIT8     0x1
#define WIDTH_BIT16    0x2
#define WIDTH_BIT32    0x4

#define BSA_UARTDR    0x0
#define BSA_UARTRSR   0x4
#define BSA_UARTFR    0x18
#define BSA_UARTLCR_H 0x2C
#define BSA_UARTCR    0x30
#define BSA_UARTIMSC  0x38
#define BSA_UARTRIS   0x3C
#define BSA_UARTMIS   0x40
#define BSA_UARTICR   0x44



#endif // __BSA_ACS_PERIPHERAL_H__
