/* ----------------------------------------------------------------------    
* Copyright (C) 2010-2014 ARM Limited. All rights reserved.    
*    
* $Date:        19. March 2015
* $Revision: 	V.1.4.5
*    
* Project: 	    CMSIS DSP Library    
* Title:	    arm_mult_f32.c    
*    
* Description:	Floating-point vector multiplication.    
*    
* Target Processor: Cortex-M4/Cortex-M3/Cortex-M0
*  
* Redistribution and use in source and binary forms, with or without 
* modification, are permitted provided that the following conditions
* are met:
*   - Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   - Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in
*     the documentation and/or other materials provided with the 
*     distribution.
*   - Neither the name of ARM LIMITED nor the names of its contributors
*     may be used to endorse or promote products derived from this
*     software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
* ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.  
* -------------------------------------------------------------------- */

#include "arm_math.h"

/**        
 * @ingroup groupMath        
 */

/**        
 * @defgroup BasicMult Vector Multiplication        
 *        
 * Element-by-element multiplication of two vectors.        
 *        
 * <pre>        
 *     pDst[n] = pSrcA[n] * pSrcB[n],   0 <= n < blockSize.        
 * </pre>        
 *        
 * There are separate functions for floating-point, Q7, Q15, and Q31 data types.        
 */

/**        
 * @addtogroup BasicMult        
 * @{        
 */

/**        
 * @brief Floating-point vector multiplication.        
 * @param[in]       *pSrcA points to the first input vector        
 * @param[in]       *pSrcB points to the second input vector        
 * @param[out]      *pDst points to the output vector        
 * @param[in]       blockSize number of samples in each vector        
 * @return none.        
 */

void arm_mult_f32 (   float32_t * pSrcA,  float32_t * pSrcB,  float32_t * pDst,  uint32_t blockSize)
{
  uint32_t blkCnt;                               /* loop counters */
#ifndef ARM_MATH_CM0_FAMILY


  float32_t   inA1, inA2, inA3, inA4;              /* temporary input variables */
  float32_t   inB1, inB2, inB3, inB4;              /* temporary input variables */
  float32_t   out1, out2, out3, out4;              /* temporary output variables */


  blkCnt = blockSize >> 2u;

  while(blkCnt > 0u)
  {
    inA1        = * pSrcA;
    inB1        = * pSrcB;
    inA2        = *(pSrcA + 1u);
    inB2        = *(pSrcB + 1u);
    out1        =   inA1 * inB1;


    inA3        = *(pSrcA + 2u);
    inB3        = *(pSrcB + 2u);
    out2        =   inA2 * inB2;
    inA4        = *(pSrcA + 3u);
    *pDst       =   out1;

    inB4        = *(pSrcB + 3u);
    out3        =  inA3 * inB3;
    *(pDst + 1u) = out2;

    out4        = inA4 * inB4;
    *(pDst + 2u) = out3;
    *(pDst + 3u) = out4;

    pSrcA      += 4u;
    pSrcB      += 4u;
    pDst       += 4u;

    blkCnt--;
  }

  /* If the blockSize is not a multiple of 4, compute any remaining output samples here.        
   ** No loop unrolling is used. */
  blkCnt = blockSize % 0x4u;

#else

  /* Run the below code for Cortex-M0 */

  /* Initialize blkCnt with number of samples */
  blkCnt = blockSize;

#endif /* #ifndef ARM_MATH_CM0_FAMILY */

  while(blkCnt > 0u)
  {
    /* C = A * B */
    /* Multiply the inputs and store the results in output buffer */
    *pDst++ = (*pSrcA++) * (*pSrcB++);

    /* Decrement the blockSize loop counter */
    blkCnt--;
  }
}

/**        
 * @} end of BasicMult group        
 */
