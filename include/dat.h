/*
 * DAT text file decoding and encoding.
 *
 * SPDX-FileCopyrightText: 2026 SombrAbsol
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef DAT_H
#define DAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Return the number of strings of a DAT file, or -1 if 'data' is not one.
 */
int64_t dat_check(const uint8_t *data, size_t size);

/*
 * Decode string 'i' of a DAT file accepted by dat_check to UTF-8, its control
 * codes written as markup. Return NULL if memory runs out.
 */
char *dat_decode(const uint8_t *data, size_t size, uint32_t i, bool japanese);

/*
 * Build a DAT file of 'count' strings encoded from UTF-8 and markup. Return
 * NULL on failure after printing the reason, 'name' naming the input.
 */
uint8_t *dat_build(const char *const *strings,
    uint32_t count,
    bool japanese,
    const char *name,
    size_t *outSize);

#endif /* DAT_H */
