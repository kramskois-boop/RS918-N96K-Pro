/*  -*-  mode: c; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4; coding: utf-8  -*-  */
/************************************************************************************
**                                                                                 **
**                               mcHF QRP Transceiver                              **
**                             K Atanassov - M0NKA 2014                            **
**                                                                                 **
**---------------------------------------------------------------------------------**
**                                                                                 **
**  File name:                                                                     **
**  Description:                                                                   **
**  Last Modified:                                                                 **
**  Licence:		GNU GPLv3                                                      **
************************************************************************************/

// Common
#include "uhsdr_board.h"
#include "profiling.h"
#include "uhsdr_hw_i2s.h"

#include "audio_driver.h"

#ifdef UI_BRD_MCHF
#include "i2s.h"
#endif

#ifdef UI_BRD_OVI40
#include "sai.h"
#endif




__UHSDR_DMAMEM __IO         dma_audio_buffer_t         audio_buf;


static uint32_t szbuf;


#ifdef USE_24_BITS
typedef int32_t audio_data_t;
#else
typedef int16_t audio_data_t;
#endif

#ifdef UI_BRD_MCHF
#define CODEC_IQ_IDX 0
#define CODEC_ANA_IDX  0
#endif

#ifdef UI_BRD_OVI40
#define CODEC_IQ_IDX 1
#define CODEC_ANA_IDX 0
#endif

void UhsdrHwI2s_Codec_ClearTxDmaBuffer()
{
  //    memset( (void*)&audio_buf.out, 0, sizeof(audio_buf.out) );

    arm_fill_f32 ( 0,  (float32_t*)  audio_buf.out,  256 ) ;
}

#ifdef DEBUG_BUILD
static void AudioDriver_DiagIsrTiming(void);   // N96K B3: defined below the DMA block handler
#endif

static  void  MchfHw_Codec_HandleBlock ( uint16_t  which )
{
#ifdef DEBUG_BUILD
    AudioDriver_DiagIsrTiming();   // N96K B3: audio real-time deadline monitor
#endif

#ifdef PROFILE_EVENTS
    // we stop during interrupt
    // at the end we start again
    // profileCycleCount_stop();
    profileTimedEventStart ( ProfileAudioInterrupt );
#endif

    static AudioSample_t *src, *dst;

#ifdef EXEC_PROFILING
    // Profiling pin (high level)
    GPIOE->BSRRL = GPIO_Pin_10;
#endif

    ts.audio_int_counter++;   // generating a time base for encoder handling

				// Transfer complete interrupt
				// Point to 2nd half of buffers
				// sz = szbuf/2;      //  128 = 256/2
    uint16_t offset = which == 0?  128 : 0;

    src = (AudioSample_t*) &audio_buf.in [offset];
    dst = (AudioSample_t*) &audio_buf.out[offset];

    AudioDriver_I2SCallback( src, dst);

#ifdef EXEC_PROFILING
    			// Profiling pin (low level)
    GPIOE->BSRRH = GPIO_Pin_10;
#endif
#ifdef PROFILE_EVENTS
			// we stopped during interrupt
			// now we start again
			// profileCycleCount_start();
    profileTimedEventStop( ProfileAudioInterrupt );
#endif

#ifdef DEBUG_BUILD
    // N96K B3: remember the longest audio ISR (in DWT cycles) for the debug INFO page. Reuse the
    // profiling timestamps of this very call; reject values which are clearly a counter wrap.
    {
        uint32_t isr_cycles = eventProfile.event[ProfileAudioInterrupt].stop - eventProfile.event[ProfileAudioInterrupt].start;
        if ( (isr_cycles < AUDIO_ISR_CYCLE_WRAP_LIMIT) && (isr_cycles > audio_diag.isr_max_cycles) )
        {
            audio_diag.isr_max_cycles = isr_cycles;
        }
    }
#endif
}

#ifdef DEBUG_BUILD
/**
 * N96K B3: audio real-time deadline monitor (called at the top of every DMA half/full block).
 * The codec DMA produces a block every AUDIO_ISR_CYCLE_BUDGET cycles, so a callback which starts
 * more than 1.25 block periods after the previous one means that the audio ISR (or the DSP chain it
 * runs) could not keep up and the DMA ring was likely overrun. A DMA stop/restart (codec restart)
 * also shows up here - such events are rare, the counter is a coarse overload indicator.
 */
static void AudioDriver_DiagIsrTiming(void)
{
    static uint32_t last_isr_start = 0;
    static bool     last_isr_valid = false;

    uint32_t now = profileCycleCount_get();
    if ( last_isr_valid && ((now - last_isr_start) > (AUDIO_ISR_CYCLE_BUDGET + AUDIO_ISR_CYCLE_BUDGET / 4u)) )
    {
        AudioDriver_DiagInc(&audio_diag.isr_late);
    }
    last_isr_start = now;
    last_isr_valid = true;
}
#endif

#ifdef UI_BRD_MCHF
/**
 * @brief HAL Handler for Codec DMA Interrupt
 */
void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    MchfHw_Codec_HandleBlock(0);
}

/**
 * @brief HAL Handler for Codec DMA Interrupt
 */
void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    MchfHw_Codec_HandleBlock(1);
}
#endif

#ifdef UI_BRD_OVI40
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hi2s)
{
    if (hi2s == &hsai_BlockA2)
    {
        MchfHw_Codec_HandleBlock(0);
    }
}




void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hi2s)
{
    if (hi2s == &hsai_BlockA2)
    {
        MchfHw_Codec_HandleBlock(1);
    }
}
#endif

void UhsdrHwI2s_Codec_StartDMA()
{
    szbuf =   BUFF_LEN_96K;   // NIZ96 2 foix memoires de 2x64 ;  2*2*64 = 256;

#ifdef UI_BRD_MCHF
    HAL_I2SEx_TransmitReceive_DMA ( &hi2s3,  (uint16_t*) audio_buf.out,  (uint16_t*) audio_buf.in, szbuf );
#endif
#ifdef UI_BRD_OVI40
    // we clean the buffers since we don't know if we are in a "cleaned" memory segement
    memset( (void*)audio_buf, 0, sizeof( audio_buf ) );

    HAL_SAI_Receive_DMA ( &hsai_BlockA1, (uint8_t*) audio_buf.in,  szbuf );
    HAL_SAI_Transmit_DMA( &hsai_BlockB1, (uint8_t*) audio_buf.out, szbuf );

    HAL_SAI_Receive_DMA ( &hsai_BlockA2, (uint8_t*) audio_buf.in,  szbuf );
    HAL_SAI_Transmit_DMA( &hsai_BlockB2, (uint8_t*) audio_buf.out, szbuf );

#endif
}


void UhsdrHwI2s_Codec_StopDMA(void)
{
#ifdef UI_BRD_MCHF
    HAL_I2S_DMAStop(&hi2s3);
#endif
#ifdef UI_BRD_OVI40
    HAL_SAI_DMAStop(&hsai_BlockA1);
    HAL_SAI_DMAStop(&hsai_BlockB1);
    HAL_SAI_DMAStop(&hsai_BlockA2);
    HAL_SAI_DMAStop(&hsai_BlockB2);
#endif
}

#if defined(UI_BRD_MCHF) && defined(DEBUG_BUILD)
// N96K B3: count audio codec DMA errors for the debug INFO page. In DMA mode the HAL reports DMA
// transfer errors through this callback (HAL_I2S_ERROR_DMA); OVR/UDR are only detected in the
// interrupt driven I2S mode, which this firmware does not use. Counting only, recovery is unchanged.
void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    if ( hi2s->ErrorCode != HAL_I2S_ERROR_NONE )  { AudioDriver_DiagInc(&audio_diag.dma_err); }
}
#endif
