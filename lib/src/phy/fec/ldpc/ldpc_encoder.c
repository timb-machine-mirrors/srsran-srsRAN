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
 * \file ldpc_encoder.c
 * \brief Definition of the LDPC encoder.
 * \author David Gregoratti
 * \date 2020
 *
 * \copyright Software Radio Systems Limited
 *
 */

#include <stdint.h>

#include "../utils_avx2.h"
#include "ldpc_enc_all.h"
#include "srslte/phy/fec/ldpc/base_graph.h"
#include "srslte/phy/fec/ldpc/ldpc_encoder.h"
#include "srslte/phy/utils/debug.h"
#include "srslte/phy/utils/vector.h"

/*! Carries out the actual destruction of the memory allocated to the encoder. */
static void free_enc_c(void* o)
{
  srslte_ldpc_encoder_t* q = o;
  if (q->pcm) {
    free(q->pcm);
  }
  if (q->ptr) {
    free(q->ptr);
  }
}

/*! Carries out the actual encoding with a non-optimized encoder. */
static int encode_c(void* o, const uint8_t* input, uint8_t* output, uint32_t input_length, uint32_t cdwd_rm_length)
{
  srslte_ldpc_encoder_t* q = o;

  if (input_length / q->bgK != q->ls) {
    perror("Dimension mismatch.\n");
    return -1;
  }
  // it must be smaller than the codeword size
  if (cdwd_rm_length > q->liftN - 2 * q->ls) {
    cdwd_rm_length = q->liftN - 2 * q->ls;
  }
  // We need at least q->bgK + 4 variable nodes to cover the high-rate region. However,
  // 2 variable nodes are systematically punctured by the encoder.
  if (cdwd_rm_length < (q->bgK + 2) * q->ls) {
    // ERROR("The rate-matched codeword should have a length at least equal to the high-rate region.\n");
    cdwd_rm_length = (q->bgK + 2) * q->ls;
    // return -1;
  }
  if (cdwd_rm_length % q->ls) {
    cdwd_rm_length = (cdwd_rm_length / q->ls + 1) * q->ls;
    // ERROR("The rate-matched codeword length should be a multiple of the lifting size.\n");
    // return -1;
  }

  // systematic bits
  int skip_in = 2 * q->ls;
  for (int k = 0; k < (q->bgK - 2) * q->ls; k++) {
    output[k] = input[skip_in + k];
  }

  preprocess_systematic_bits(q, input);

  q->encode_high_rate(q, output);

  // When computing the number of layers, we need to recall that the standard always removes
  // the first two variable nodes from the final codeword.
  uint8_t n_layers = cdwd_rm_length / q->ls - q->bgK + 2;

  encode_ext_region(q, output, n_layers);

  return 0;
}

/*! Initializes a non-optimized encoder. */
static int init_c(srslte_ldpc_encoder_t* q)
{
  int ls_index = get_ls_index(q->ls);

  if (ls_index == VOID_LIFTSIZE) {
    ERROR("Invalid lifting size %d\n", q->ls);
    return -1;
  }

  if (q->bg == BG1 && ls_index != 6) {
    q->encode_high_rate = encode_high_rate_case1;
  } else if (q->bg == BG1 && ls_index == 6) {
    q->encode_high_rate = encode_high_rate_case2;
  } else if (q->bg == BG2 && ls_index != 3 && ls_index != 7) {
    q->encode_high_rate = encode_high_rate_case3;
  } else if (q->bg == BG2 && (ls_index == 3 || ls_index == 7)) {
    q->encode_high_rate = encode_high_rate_case4;
  } else {
    ERROR("Invalid lifting size %d and/or Base Graph %d\n", q->ls, q->bg + 1);
    return -1;
  }

  q->free = free_enc_c;

  q->ptr = srslte_vec_u8_malloc(q->bgM * q->ls);
  if (!q->ptr) {
    perror("malloc");
    free_enc_c(q);
    return -1;
  }

  q->encode = encode_c;

  return 0;
}

#ifdef LV_HAVE_AVX2
/*! Carries out the actual destruction of the memory allocated to the encoder. */
static void free_enc_avx2(void* o)
{
  srslte_ldpc_encoder_t* q = o;
  if (q->pcm) {
    free(q->pcm);
  }
  if (q->ptr) {
    delete_ldpc_enc_avx2(q->ptr);
  }
}

/*! Carries out the actual encoding with an optimized encoder. */
static int encode_avx2(void* o, const uint8_t* input, uint8_t* output, uint32_t input_length, uint32_t cdwd_rm_length)
{
  srslte_ldpc_encoder_t* q = o;

  if (input_length / q->bgK != q->ls) {
    perror("Dimension mismatch.\n");
    return -1;
  }

  // it must be smaller than the codeword size
  if (cdwd_rm_length > q->liftN - 2 * q->ls) {
    cdwd_rm_length = q->liftN - 2 * q->ls;
  }
  // We need at least q->bgK + 4 variable nodes to cover the high-rate region. However,
  // 2 variable nodes are systematically punctured by the encoder.
  if (cdwd_rm_length < (q->bgK + 2) * q->ls) {
    // ERROR("The rate-matched codeword should have a length at least equal to the high-rate region.\n");
    cdwd_rm_length = (q->bgK + 2) * q->ls;
    // return -1;
  }
  if (cdwd_rm_length % q->ls) {
    cdwd_rm_length = (cdwd_rm_length / q->ls + 1) * q->ls;
    // ERROR("The rate-matched codeword length should be a multiple of the lifting size.\n");
    // return -1;
  }

  load_avx2(q->ptr, input, q->bgK, q->bgN, q->ls);

  preprocess_systematic_bits_avx2(q);

  q->encode_high_rate_avx2(q);

  // When computing the number of layers, we need to recall that the standard always removes
  // the first two variable nodes from the final codeword.
  uint8_t n_layers = cdwd_rm_length / q->ls - q->bgK + 2;

  encode_ext_region_avx2(q, n_layers);

  return_codeword_avx2(q->ptr, output, n_layers + q->bgK, q->ls);

  return 0;
}

/*! Initializes an optimized encoder. */
static int init_avx2(srslte_ldpc_encoder_t* q)
{
  int ls_index = get_ls_index(q->ls);

  if (ls_index == VOID_LIFTSIZE) {
    ERROR("Invalid lifting size %d\n", q->ls);
    return -1;
  }

  if (q->bg == BG1 && ls_index != 6) {
    q->encode_high_rate_avx2 = encode_high_rate_case1_avx2;
  } else if (q->bg == BG1 && ls_index == 6) {
    q->encode_high_rate_avx2 = encode_high_rate_case2_avx2;
  } else if (q->bg == BG2 && ls_index != 3 && ls_index != 7) {
    q->encode_high_rate_avx2 = encode_high_rate_case3_avx2;
  } else if (q->bg == BG2 && (ls_index == 3 || ls_index == 7)) {
    q->encode_high_rate_avx2 = encode_high_rate_case4_avx2;
  } else {
    ERROR("Invalid lifting size %d and/or Base Graph %d\n", q->ls, q->bg + 1);
    return -1;
  }

  q->free = free_enc_avx2;

  if ((q->ptr = create_ldpc_enc_avx2(q)) == NULL) {
    perror("Create_ldpc_enc\n");
    free_enc_avx2(q);
    return -1;
  }

  q->encode = encode_avx2;

  return 0;
}

/*! Carries out the actual destruction of the memory allocated to the encoder. */
static void free_enc_avx2long(void* o)
{
  srslte_ldpc_encoder_t* q = o;
  if (q->pcm) {
    free(q->pcm);
  }
  if (q->ptr) {
    delete_ldpc_enc_avx2long(q->ptr);
  }
}

/*! Carries out the actual encoding with an optimized encoder. */
static int
encode_avx2long(void* o, const uint8_t* input, uint8_t* output, uint32_t input_length, uint32_t cdwd_rm_length)
{
  srslte_ldpc_encoder_t* q = o;

  if (input_length / q->bgK != q->ls) {
    perror("Dimension mismatch.\n");
    return -1;
  }

  // it must be smaller than the codeword size
  if (cdwd_rm_length > q->liftN - 2 * q->ls) {
    cdwd_rm_length = q->liftN - 2 * q->ls;
  }
  // We need at least q->bgK + 4 variable nodes to cover the high-rate region. However,
  // 2 variable nodes are systematically punctured by the encoder.
  if (cdwd_rm_length < (q->bgK + 2) * q->ls) {
    // ERROR("The rate-matched codeword should have a length at least equal to the high-rate region.\n");
    cdwd_rm_length = (q->bgK + 2) * q->ls;
    // return -1;
  }
  if (cdwd_rm_length % q->ls) {
    cdwd_rm_length = (cdwd_rm_length / q->ls + 1) * q->ls;
    // ERROR("The rate-matched codeword length should be a multiple of the lifting size.\n");
    // return -1;
  }
  load_avx2long(q->ptr, input, q->bgK, q->bgN, q->ls);

  preprocess_systematic_bits_avx2long(q);

  q->encode_high_rate_avx2(q);

  // When computing the number of layers, we need to recall that the standard always removes
  // the first two variable nodes from the final codeword.
  uint8_t n_layers = cdwd_rm_length / q->ls - q->bgK + 2;

  encode_ext_region_avx2long(q, n_layers);

  return_codeword_avx2long(q->ptr, output, n_layers + q->bgK, q->ls);

  return 0;
}

/*! Initializes an optimized encoder. */
static int init_avx2long(srslte_ldpc_encoder_t* q)
{
  int ls_index = get_ls_index(q->ls);

  if (ls_index == VOID_LIFTSIZE) {
    ERROR("Invalid lifting size %d\n", q->ls);
    return -1;
  }

  if (q->bg == BG1 && ls_index != 6) {
    q->encode_high_rate_avx2 = encode_high_rate_case1_avx2long;
  } else if (q->bg == BG1 && ls_index == 6) {
    q->encode_high_rate_avx2 = encode_high_rate_case2_avx2long;
  } else if (q->bg == BG2 && ls_index != 3 && ls_index != 7) {
    q->encode_high_rate_avx2 = encode_high_rate_case3_avx2long;
  } else if (q->bg == BG2 && (ls_index == 3 || ls_index == 7)) {
    q->encode_high_rate_avx2 = encode_high_rate_case4_avx2long;
  } else {
    ERROR("Invalid lifting size %d and/or Base Graph %d\n", q->ls, q->bg + 1);
    return -1;
  }

  q->free = free_enc_avx2long;

  if ((q->ptr = create_ldpc_enc_avx2long(q)) == NULL) {
    perror("Create_ldpc_enc\n");
    free_enc_avx2long(q);
    return -1;
  }

  q->encode = encode_avx2long;

  return 0;
}

#endif

int srslte_ldpc_encoder_init(srslte_ldpc_encoder_t*     q,
                             srslte_ldpc_encoder_type_t type,
                             srslte_basegraph_t         bg,
                             uint16_t                   ls)
{

  switch (bg) {
    case BG1:
      q->bgN = BG1Nfull;
      q->bgM = BG1M;
      break;
    case BG2:
      q->bgN = BG2Nfull;
      q->bgM = BG2M;
      break;
    default:
      ERROR("Base Graph BG%d does not exist\n", bg + 1);
      return -1;
  }
  q->bg  = bg;
  q->bgK = q->bgN - q->bgM;

  q->ls    = ls;
  q->liftK = ls * q->bgK;
  q->liftM = ls * q->bgM;
  q->liftN = ls * q->bgN;

  q->pcm = srslte_vec_u16_malloc(q->bgM * q->bgN);
  if (!q->pcm) {
    perror("malloc");
    return -1;
  }
  if (create_compact_pcm(q->pcm, NULL, q->bg, q->ls) != 0) {
    perror("Create PCM");
    return -1;
  }

  switch (type) {
    case SRSLTE_LDPC_ENCODER_C:
      return init_c(q);
#ifdef LV_HAVE_AVX2
    case SRSLTE_LDPC_ENCODER_AVX2:
      if (ls <= SRSLTE_AVX2_B_SIZE) {
        return init_avx2(q);
      } else {
        return init_avx2long(q);
      }
#endif // LV_HAVE_AVX2
    default:
      return -1;
  }
}

void srslte_ldpc_encoder_free(srslte_ldpc_encoder_t* q)
{
  if (q->free) {
    q->free(q);
  }
  bzero(q, sizeof(srslte_ldpc_encoder_t));
}

int srslte_ldpc_encoder_encode(srslte_ldpc_encoder_t* q, const uint8_t* input, uint8_t* output, uint32_t input_length)
{
  return q->encode(q, input, output, input_length, q->liftN - 2 * q->ls);
}

int srslte_ldpc_encoder_encode_rm(srslte_ldpc_encoder_t* q,
                                  const uint8_t*         input,
                                  uint8_t*               output,
                                  uint32_t               input_length,
                                  uint32_t               cdwd_rm_length)
{
  return q->encode(q, input, output, input_length, cdwd_rm_length);
}
