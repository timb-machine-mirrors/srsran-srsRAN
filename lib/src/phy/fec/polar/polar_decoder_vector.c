/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

/*!
 * \file polar_decoder_vector.c
 * \brief Definition of the polar decoder vectorizable functions.
 * \author Jesus Gomez
 * \date 2020
 *
 * \copyright Software Radio Systems Limited
 *
 */

#include "math.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> //abs function

/*!
 * Sign of a real number.
 */
static int sgn(float v)
{
  return (v > 0) - (v < 0);
}

/*!
 *  Returns 1 if \f$ (x < 0) \f$ and 0 if \f$ (x >= 0) \f$.
 */

#define hard_bit                                                                                                       \
  {                                                                                                                    \
    int s = 0;                                                                                                         \
    for (uint16_t i = 0; i < len; ++i) {                                                                               \
      s = sgn(x[i]);                                                                                                   \
      if (s == 0) {                                                                                                    \
        z[i] = 0;                                                                                                      \
      } else {                                                                                                         \
        z[i] = (uint8_t)(1 - s) / 2;                                                                                   \
      }                                                                                                                \
    }                                                                                                                  \
  }

void srslte_vec_function_f_fff(const float* x, const float* y, float* z, const uint16_t len)
{
  float L0      = NAN;
  float L1      = NAN;
  float absL0   = NAN;
  float absL1   = NAN;
  float sgnL0L1 = NAN;

  for (int i = 0; i < len; i++) {
    L0      = x[i];
    L1      = y[i];
    absL0   = fabsf(L0);
    absL1   = fabsf(L1);
    sgnL0L1 = sgn(L0) * sgn(L1);
    if (absL0 >= absL1) {
      L0 = sgnL0L1 * absL1;
    } else {
      L0 = sgnL0L1 * absL0;
    }
    z[i] = L0;
  }
}

void srslte_vec_function_f_sss(const int16_t* x, const int16_t* y, int16_t* z, const uint16_t len)
{

  int16_t L0      = 0;
  int16_t L1      = 0;
  int16_t absL0   = 0;
  int16_t absL1   = 0;
  int16_t sgnL0L1 = 0;

  for (int i = 0; i < len; i++) {
    L0      = x[i];
    L1      = y[i];
    absL0   = abs(L0);
    absL1   = abs(L1);
    sgnL0L1 = sgn(L0) * sgn(L1);
    if (absL0 >= absL1) {
      L0 = sgnL0L1 * absL1;
    } else {
      L0 = sgnL0L1 * absL0;
    }
    z[i] = L0;
  }
}

void srslte_vec_function_f_ccc(const int8_t* x, const int8_t* y, int8_t* z, const uint16_t len)
{
  int8_t L0      = 0;
  int8_t L1      = 0;
  int8_t absL0   = 0;
  int8_t absL1   = 0;
  int8_t sgnL0L1 = 0;

  for (int i = 0; i < len; i++) {
    L0      = x[i];
    L1      = y[i];
    absL0   = abs(L0);
    absL1   = abs(L1);
    sgnL0L1 = sgn(L0) * sgn(L1);
    if (absL0 >= absL1) {
      L0 = sgnL0L1 * absL1;
    } else {
      L0 = sgnL0L1 * absL0;
    }
    z[i] = L0;
  }
}

void srslte_vec_hard_bit_fc(const float* x, uint8_t* z, const uint16_t len)
{
  hard_bit;
}

void srslte_vec_hard_bit_sc(const int16_t* x, uint8_t* z, const uint16_t len)
{
  hard_bit;
}

void srslte_vec_hard_bit_cc(const int8_t* x, uint8_t* z, const uint16_t len)
{
  hard_bit;
}

void srslte_vec_function_g_bfff(const uint8_t* b, const float* x, const float* y, float* z, const uint16_t len)
{

  float  L0 = NAN;
  float  L1 = NAN;
  int8_t V  = 0;

  for (int i = 0; i < len; i++) {
    L0   = x[i];
    L1   = y[i];
    V    = -2 * b[i] + 1; // (warning!) changes size from uint8_t to int8_t
    L0   = L1 + V * L0;
    z[i] = L0;
  }
}

void srslte_vec_function_g_bsss(const uint8_t* b, const int16_t* x, const int16_t* y, int16_t* z, const uint16_t len)
{

  int16_t L0 = 0;
  int16_t L1 = 0;
  int8_t  V  = 0;

  long tmp = 0;

  for (int i = 0; i < len; i++) {
    L0 = x[i];
    L1 = y[i];
    V  = -2 * b[i] + 1; // (warning!) changes size from uint8_t to int8_t

    tmp = (long)L1 + V * L0;
    if (tmp > 32767) {
      tmp = 32767;
    }
    if (tmp < -32767) {
      tmp = -32767;
    }
    L0 = (int16_t)tmp;

    z[i] = L0;
  }
}

void srslte_vec_function_g_bccc(const uint8_t* b, const int8_t* x, const int8_t* y, int8_t* z, const uint16_t len)
{

  int8_t L0 = 0;
  int8_t L1 = 0;
  int8_t V  = 0;

  long tmp = 0;

  for (int i = 0; i < len; i++) {
    L0 = x[i];
    L1 = y[i];
    V  = -2 * b[i] + 1; // (warning!) changes size from uint8_t to int8_t

    tmp = (long)L1 + V * L0;
    if (tmp > 127) {
      tmp = 127;
    }
    if (tmp < -127) {
      tmp = -127;
    }
    L0 = (int8_t)tmp;

    z[i] = L0;
  }
}
