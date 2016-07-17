/*
 * Copyright (c) 2008 Travis Geiselbrecht
 *
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <asm.h>

FUNCTION(uefiapi_fn_wrapper_template)
    // backup state
    push {r11,lr}
    push {r0-r3}

    // entry check
	adr r11, fn_entry
    ldr r11, [r11]
    blx r11
    mov r11, r0
    pop {r0-r3}
    push {r11}

    // run original function
	adr r11, fn_hookee
    ldr r11, [r11]
    blx r11
    pop {r11}
    push {r0-r3}

    // exit check
    mov r0, r11
	adr r11, fn_exit
    ldr r11, [r11]
    blx r11
    pop {r0-r3}

    // return
    pop {r11, pc}

fn_entry:
    .word 0
fn_hookee:
    .word 0
fn_exit:
    .word 0

.global uefiapi_fn_wrapper_template_end
uefiapi_fn_wrapper_template_end:

