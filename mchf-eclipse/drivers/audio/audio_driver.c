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
#include <assert.h>
#include "uhsdr_board.h"
#include "ui_driver.h"
#include "profiling.h"

#include <stdio.h>
#include <math.h>
#include "codec.h"

#include "cw_gen.h"

#include <limits.h>
#include "softdds.h"

#include "audio_driver.h"
#include "audio_nr.h"
#include "audio_management.h"
#include "radio_management.h"
#include "usbd_audio_if.h"
#include "ui_spectrum.h"
#include "filters.h"
#include "ui_lcd_hy28.h"
#include "ui_configuration.h"
#include "uhsdr_hw_i2s.h"
#include "rtty.h"
#include "psk.h"
#include "cw_decoder.h"
#include "freedv_uhsdr.h"

#ifdef USE_CONVOLUTION
#include "audio_convolution.h"
#endif

typedef struct
{
	// AGC
	//#define   MAX_SAMPLE_RATE     (24000.0)
	//#define   MAX_N_TAU           (8)
	//#define   MAX_TAU_ATTACK      (0.01)
	//#define   RB_SIZE       (int) (MAX_SAMPLE_RATE * MAX_N_TAU * MAX_TAU_ATTACK + 1)
#define    AGC_WDSP_RB_SIZE   96     // 192
	//int8_t    AGC_mode = 2;
	int         pmode;  // = 1; // if 0, calculate magnitude by max(|I|, |Q|), if 1, calculate sqrtf(I*I+Q*Q)
	float32_t   out_sample;
	float32_t   abs_out_sample;
	float32_t   tau_attack;
	float32_t   tau_decay;
	int         n_tau;
	float32_t   max_gain;
	float32_t   var_gain;
	float32_t   fixed_gain; // = 1.0;
	float32_t   max_input;
	float32_t   out_targ;
	float32_t   tau_fast_backaverage;
	float32_t   tau_fast_decay;
	float32_t   pop_ratio;
	//uint8_t   hang_enable;
	float32_t   tau_hang_backmult;
	float32_t   hangtime;
	float32_t   hang_thresh;
	float32_t   tau_hang_decay;
	float32_t   ring    [  AGC_WDSP_RB_SIZE];    //  96 // 192
	float32_t   abs_ring[  AGC_WDSP_RB_SIZE];    //  96 // 192//  abs_ring is half the size of ring
	//assign constants
	int         ring_buffsize; // 192;
	//do one-time initialization
	int         out_index; // = -1;
	float32_t   ring_max; // = 0.0;
	float32_t   ring_max_avg; // = 0.0;
	float32_t   save_volts; // = 0.0;
	float32_t   fast_backaverage; // = 0.0;
	float32_t   hang_backaverage; // = 0.0;
	int         hang_counter; // = 0;
	uint8_t     decay_type; // = 0;
	uint8_t     state; // = 0;
	int         attack_buffsize;
	uint32_t    in_index;
	float32_t   attack_mult;
	float32_t   decay_mult;
	float32_t   fast_decay_mult;
	float32_t   fast_backmult;
	float32_t   onemfast_backmult;
	float32_t   out_target;
	float32_t   min_volts;
	float32_t   inv_out_target;
	float32_t   tmp;
	float32_t   slope_constant;
	float32_t   inv_max_input;
	float32_t   hang_level;
	float32_t   hang_backmult;
	float32_t   onemhang_backmult;
	float32_t   hang_decay_mult;

	//******************************************************************
	//************************ AGC  Nizar ******************************
	//******************************************************************
	float32_t  thresh_inp ;   // seuil du signal d'entrée
	float32_t  thresh_out ;   // gain au seuil du signal d'entrée
	float32_t  pente      ;

} agc_variables_t;

agc_variables_t   agc_wdsp;

// SSB filters - now handled in ui_driver to allow I/Q phase adjustment



    static        void AudioDriver_Tx_Filter_Audio ( bool do_bandpass, bool pass_tout, bool do_bass_treble, float32_t* inBlock, float32_t* outBlock, const uint16_t blockSize);


#define LMS2_NOTCH_STATE_ARRAY_SIZE (DSP_NOTCH_NUMTAPS_MAX + IQ_BLOCK_SIZE)

#ifdef USE_LMS_AUTONOTCH
typedef struct
{
    float32_t   errsig2[IQ_BLOCK_SIZE];
    arm_lms_norm_instance_f32	lms2Norm_instance;
    arm_lms_instance_f32	    lms2_instance;
    float32_t	                lms2StateF32[LMS2_NOTCH_STATE_ARRAY_SIZE];
    float32_t	                lms2NormCoeff_f32[DSP_NOTCH_NUMTAPS_MAX];
    float32_t	                lms2_nr_delay[DSP_NOTCH_BUFLEN_MAX];
} LMSData;
#endif

static float32_t	__MCHF_SPECIALMEM   audio_delay_buffer	[AUDIO_DELAY_BUFSIZE];  // 320

static void AudioDriver_ClearAudioDelayBuffer()
{
    arm_fill_f32 ( 0, audio_delay_buffer, AUDIO_DELAY_BUFSIZE );   // 320 x float32_t
}

#ifdef DEBUG_BUILD
// N96K B3: definition of the cheap audio/CPU diagnostics declared in audio_driver.h.
AudioDiag_t audio_diag;
#endif

// N96K B3: fixed size of the NR decimation look-ahead buffer used in
// AudioDriver_RxProcessorNoiseReduction(). Every call site passes blockSizeDecim = 8 (BS_8) and the
// optional internal decimation-by-two halves it to 4, so a fixed buffer removes the VLA from the
// audio DMA ISR stack (audit F4) without changing behaviour.
#define NR_DEC_BUFFER_SIZE 8

#if NR_DEC_BUFFER_SIZE < 8
#error "N96K B3: NR_DEC_BUFFER_SIZE must cover blockSizeDecim == 8 (BS_8)"
#endif

// This is a fast approximation to log2()
// Y = C[0]*F*F*F + C[1]*F*F + C[2]*F + C[3] + E;
//log10f is exactly log2(x)/log2(10.0f)
// log10f_fast(x) =(log2f_approx(x)*0.3010299956639812f)
float log10f_fast(float X) {
    float Y, F;
    int E;
    F = frexpf(fabsf(X), &E);
    Y = 1.23149591368684f;
    Y *= F;
    Y += -4.11852516267426f;
    Y *= F;
    Y += 6.02197014179219f;
    Y *= F;
    Y += -3.13396450166353f;
    Y += E;
    return(Y * 0.3010299956639812f);
}

//
// Audio RX - Decimator

arm_fir_decimate_instance_f32             DECIMATE_RX_I;
float32_t           __MCHF_SPECIALMEM     decimState_I[ 32 + 43 + 1  ];  // 75
// Audio RX - Decimator in Q-path

arm_fir_decimate_instance_f32             DECIMATE_RX_Q;
float32_t           __MCHF_SPECIALMEM     decimState_Q[ 32 + 43 + 1 ];  // 75

arm_fir_decimate_instance_f32            DECIMATE_TX;
float32_t           __MCHF_SPECIALMEM    decimState_Tx[ 32 + 43];   // 75

// Decimator for Zoom FFT
static	arm_fir_decimate_instance_f32	DECIMATE_ZOOM_FFT_I;
float32_t			__MCHF_SPECIALMEM   decimZoomFFTIState[ 32 + FIR_RXAUDIO_NUM_TAPS ]; // 48 = 32 + 16

// Decimator for Zoom FFT
static	arm_fir_decimate_instance_f32	DECIMATE_ZOOM_FFT_Q;
float32_t			__MCHF_SPECIALMEM   decimZoomFFTQState[ 32 + FIR_RXAUDIO_NUM_TAPS ];   // 48 = 32 + 16


// Audio RX - RIF Interpolator 12Khz --> 48Khz vers Haut parleur (pas critique) RIF 16tap
static	 arm_fir_interpolate_instance_f32  Rx_FIR_INTERPOLATE_12_to_48;            // INTERPOLATE_RX[NUM_AUDIO_CHANNELS];
float32_t  __MCHF_SPECIALMEM               Rx_interp_State_12_to_48 [ 8 + 8 ];  // 14 = 8 + 24/4  pour 24Khz et 12Khz, jusqu'à 16 taps


// Audio TX-Interpolator  12Khz -----> 48 Khz  l'un pour la Voie "I" et l'autre pour la voie "Q"
// LPF FIR Taps=24,  freq coupure à 4200 Hz @ -3db

static	   arm_fir_interpolate_instance_f32  Tx_FIR_INTERPOLATE_12_to_48 [ 2 ];             // INTERPOLATE_RX[NUM_AUDIO_CHANNELS];
float32_t  __MCHF_SPECIALMEM                 Tx_interp_State_12_to_48    [ 2 ] [ 8 + 6 ];  // 14 = 8 + 24/4  // seulement 12Khz



// Variables pour le filtrage LPF pour interpolation 6Khz---> 12Khz du Noise Reduction FC=2.8 Khz  IIR 8em ordre
arm_iir_lattice_instance_f32    IIR_NR_Interpolation;
float32_t   __MCHF_SPECIALMEM   IIR_Noise_Reduction_Interpol_PState[ 9 + 8  ];  // 16 = 8 (blocksize) + 8 (ordre IIR)


		//*** LPF  FIR (taps 4) pour Decimation Noise Reduction NR 12Khz--> 6Khz  (sorte d'egualisation favorisant les basses )
		//*** Fs = 12ksps, Fstop = 2k7, we lowpass-filtered the audio already in the main aido path (IIR),

static  arm_fir_decimate_instance_f32   DECIMATE_NR;                   // Fs= 12Khz ( blocksize = 8 )
static  arm_fir_instance_f32            Fir_NR_PRES_FILTRAGE;          // filtrage passe bas mais sans decimation
static  float32_t  __MCHF_SPECIALMEM    FIR_decim_NR_State[ 8 + 4 ];   // 12 = 8 points + 4 taps
static  float32_t                       FIR_NR_decimate_coeffs [ 4 ] = { 0.099144206287089282, 0.492752007869707798, 0.492752007869707798, 0.099144206287089282 };



// #define NR_INTERPOLATE_NO_TAPS   40
// static	arm_fir_interpolate_instance_f32 INTERPOLATE_NR; // Fs= 12Khz ( blocksize = 8 )
// float32_t			interplNRState[8 + 20];              // 8 points + 20 Taps = 40/2  Interpolation par X2


#define IIR_RX_STATE_ARRAY_SIZE    ( 32 + IIR_RXAUDIO_NUM_STAGES_MAX )  // 44 = 32 + 12
// variables for RX IIR filters
static float32_t	__MCHF_SPECIALMEM	iir_rx_State [ NUM_AUDIO_CHANNELS ] [ IIR_RX_STATE_ARRAY_SIZE ]; // 44
static  arm_iir_lattice_instance_f32	IIR_PreFilter[ NUM_AUDIO_CHANNELS ];


// Variable for HPF  IIR filter 3em ordre // 50Hz  75Hz  100Hz  150Hz  200Hz
static arm_iir_lattice_instance_f32	  IIR_Rx_HP_Filter;
static float32_t  __MCHF_SPECIALMEM   iir_Rx_HPF_State [ 8 + 4 ];         // 36 = 8 points  + 4 em ordre fonctionne avec decimation 1/2/4



					// Variable for LPF  IIR filter 4em ordre // 16.5Khz  pour decimation Rx 96Khz --> 48Khz
static arm_iir_lattice_instance_f32	  IIR_Rx_LPF_16K_Filter_I;
static float32_t   __MCHF_SPECIALMEM  iir_Rx_LPF_16K_I_State [ 64 + 4 + 1 ];   // 68 = 64 points + 4em ordre

static arm_iir_lattice_instance_f32	  IIR_Rx_LPF_16K_Filter_Q;
static float32_t  __MCHF_SPECIALMEM   iir_Rx_LPF_16K_Q_State [ 64 + 4 + 1 ];   // 68 = 64 points + 4em ordre


static  arm_iir_lattice_instance_f32   IIR_Rx_LP_Filter_2K6;
static  float32_t  __MCHF_SPECIALMEM   iir_Rx_LPF_2K6_FM [ 32 + 4 ];   // 35 = 32 points + 3em ordre

static  arm_iir_lattice_instance_f32   IIR_Rx_HP_Filter_22Hz;
static  float32_t  __MCHF_SPECIALMEM   IIR_Rx_HP_Filter_22Hz_state[32+3];

// static float32_t                      iir_Tx_LPF_5K7_state [ 64 + 5 ];   // 68 = 64 points + 4em ordre
// static arm_iir_lattice_instance_f32	 IIR_Tx_LPF_5K7_Filter;

					// LPF  IIR 4em ordre pour Microphone decimation 96Khz ---> 48 Khz  Fc = 4Khz
static arm_iir_lattice_instance_f32	  IIR_Tx_LPF_3K84_Filter;
static float32_t   __MCHF_SPECIALMEM  iir_Tx_LPF_3K84_State [ 64 + 4 ];     // 68 = 64 points + 4em ordre


// variables for RX antialias IIR filter
static float32_t  __MCHF_SPECIALMEM   iir_aa_State [ NUM_AUDIO_CHANNELS ][ IIR_RX_STATE_ARRAY_SIZE ]; // 44
static arm_iir_lattice_instance_f32	  IIR_AntiAlias[ NUM_AUDIO_CHANNELS ];

// variables for RX treble shelf IIR biquad filter at 48Khz
arm_biquad_casd_df1_inst_f32   IIR_biquad_Rx_Equaliser_Treble_48Khz  =  // l'un pour FM l'autre pour SSB
{
		.numStages = 1,
		.pCoeffs = (float32_t *)(float32_t [])
		{
			1,0,0,0,0
		}, // 1 x 5 = 5 coefficients

		.pState = (float32_t *)(float32_t [])
		{
			0,0,0,0
		} // 1 x 4 = 4 state variables
};


arm_biquad_casd_df1_inst_f32   IIR_biquad_Rx_Equaliser_Bass_decim =
{
		.numStages = 1,
		.pCoeffs = (float32_t *)(float32_t [])
		{
			1,0,0,0,0
		},

		.pState = (float32_t *)(float32_t [])
		{
			0,0,0,0
		}
};



arm_biquad_casd_df1_inst_f32   IIR_biquad_Rx_Manuel_Notch_decim  =

{
		.numStages = 1,
		.pCoeffs = (float32_t *)(float32_t [])
		{
			1,0,0,0,0
		}, // 1 x 5 = 5 coefficients

		.pState = (float32_t *)(float32_t [])
		{
			0,0,0,0
		} // 1 x 4 = 4 state variables
};


arm_biquad_casd_df1_inst_f32   IIR_biquad_Rx_Manuel_Peak_decim  =

{
		.numStages = 1,
		.pCoeffs = (float32_t *)(float32_t [])
		{
			1,0,0,0,0
		}, // 1 x 5 = 5 coefficients

		.pState = (float32_t *)(float32_t [])
		{
			0,0,0,0
		} // 1 x 4 = 4 state variables
};





// variables for RX treble shelf IIR biquad filter avec decimation 12khz // NIZZZ  pour SSB avec decimation  12khz
arm_biquad_casd_df1_inst_f32   IIR_biquad_Rx_Treble_decim =

{
		.numStages = 1,
		.pCoeffs = (float32_t *)(float32_t [])
		{
			1,0,0,0,0
		}, // 1 x 5 = 5 coefficients

		.pState = (float32_t *)(float32_t [])
		{
			0,0,0,0
		} // 1 x 4 = 4 state variables
};



// variables for TX Bass , Medium & Treble adjustment IIR biquad filter
static arm_biquad_casd_df1_inst_f32   IIR_TX_biquad =
{
        .numStages = 3,
        .pCoeffs = (float32_t *)(float32_t [])
        {
            1,0,0,0,0,  1,0,0,0,0,  1,0,0,0,0
        }, // 3 x 5 = 15 coefficients

        .pState = (float32_t *)(float32_t [])
        {
            0,0,0,0,   0,0,0,0,   0,0,0,0
        } // 3 x 4 = 12 state variables
};



// variables for ZoomFFT lowpass filtering
static arm_biquad_casd_df1_inst_f32 IIR_biquad_Zoom_FFT_I =
{
        .numStages = 4,
        .pCoeffs = (float32_t *)(float32_t [])
        {
            1,0,0,0,0,  1,0,0,0,0 // passthru
        }, // 2 x 5 = 10 coefficients

        .pState = (float32_t *)(float32_t [])
        {
            0,0,0,0,   0,0,0,0,    0,0,0,0,   0,0,0,0
        } // 4 x 4 = 16 state variables
};


static arm_biquad_casd_df1_inst_f32  IIR_biquad_Zoom_FFT_Q =
{
        .numStages = 4,
        .pCoeffs = (float32_t *)(float32_t [])
        {
            1,0,0,0,0,  1,0,0,0,0 // passthru
        }, // 2 x 5 = 10 coefficients

        .pState = (float32_t *)(float32_t [])
        {
            0,0,0,0,   0,0,0,0,   0,0,0,0,   0,0,0,0
        } // 4 x 4 = 16 state variables
};


/*
// 12ksps, Fstop = 2k7, KAISER, 40 taps, a good interpolation filter after the interpolation, maybe too many taps?

static float32_t NR_interpolate_coeffs [ NR_INTERPOLATE_NO_TAPS ] =
{
		-495.0757586677611930E-6,
		0.001320676868426568,
		0.001533845835568487,
		-0.002357633129357554,
		-0.003572560455091757,
		0.003388797052024843,
		0.007032840952358404,
		-0.003960820803871866,
		-0.012365795129023015,
		0.003357357660531775,
		0.020101326014980946,
		-475.4964584295063900E-6,
		-0.031094910247864812,
		-0.006597041050034579,
		0.047436525317202147,
		0.022324808965607446,
		-0.076541709512474090,
		-0.064246467306504046,
		0.167750545742874818,
		0.427794841657261171,
		0.427794841657261171,
		0.167750545742874818,
		-0.064246467306504046,
		-0.076541709512474090,
		0.022324808965607446,
		0.047436525317202147,
		-0.006597041050034579,
		-0.031094910247864812,
		-475.4964584295063900E-6,
		0.020101326014980946,
		0.003357357660531775,
		-0.012365795129023015,
		-0.003960820803871866,
		0.007032840952358404,
		0.003388797052024843,
		-0.003572560455091757,
		-0.002357633129357554,
		0.001533845835568487,
		0.001320676868426568,
		-495.0757586677611930E-6
       };

       */

//***************************************************************************************************
//***** Filtre RIF 40 taps  2734 Hz/ 12Khz d'antirepliement pour interpolation 6Khz --> 12 Khz du Noise Reduction "DNR"
//***  12ksps, Fstop 2k734, 40 taps, Park McClellan , windows off, (0.140 iteration ) Atten < -61.4 db pour freq > 3Khz
//**** a good interpolation filter after the interpolation, maybe too many taps?
//***  le dernier utilisé avant masquage le 27/04/2023 pour remplacement par un IIR 8em ordre
/*
static float32_t NR_interpolate_coeffs [ NR_INTERPOLATE_NO_TAPS ] =
{
		811.6743165864976390E-6,
		 0.003675395446555318,
		 0.002848845432697724,
		-0.003090360482475297,
		-0.005041415266402156,
		 0.003665583128648696,
		 0.008903026700318787,
		-0.003477375678753858,
		-0.014430435245624741,
		 0.001903858269779488,
		 0.021974780534098449,
		 0.001969440485851660,
		-0.032351881204287697,
		-0.009847522816288404,
		 0.047712576456631642,
		 0.026011884863363276,
		-0.075620923221922584,
		-0.067880317745987712,
		 0.165636849428262123,
		 0.430871794558806753,
		 0.430871794558806753,
		 0.165636849428262123,
		-0.067880317745987712,
		-0.075620923221922584,
		 0.026011884863363276,
		 0.047712576456631642,
		-0.009847522816288404,
		-0.032351881204287697,
		 0.001969440485851660,
		 0.021974780534098449,
		 0.001903858269779488,
		-0.014430435245624741,
		-0.003477375678753858,
		 0.008903026700318787,
		 0.003665583128648696,
		-0.005041415266402156,
		-0.003090360482475297,
		 0.002848845432697724,
		 0.003675395446555318,
		 811.6743165864976390E-6,
       };
*/

//*********************************************************************************************




// this is wrong! Interpolation filters act at the sample rate AFTER the interpolation, in this case at 12ksps
// 6ksps, Fstop = 2k65, KAISER
//static float32_t NR_interpolate_coeffs [NR_INTERPOLATE_NO_TAPS] = {-903.6623076669911820E-6, 0.001594488333496738,-0.002320508982899863, 0.002832351511451895,-0.002797105957386612, 0.001852836963547170, 308.6133633078010230E-6,-0.003842008360761881, 0.008649943961959465,-0.014305251526745446, 0.020012524686320185,-0.024618364878703208, 0.026664997481476788,-0.024458388333600374, 0.016080841021827566, 818.1032282579135430E-6,-0.029933800539235892, 0.079833661336890141,-0.182038248016552551, 0.626273078268197225, 0.626273078268197225,-0.182038248016552551, 0.079833661336890141,-0.029933800539235892, 818.1032282579135430E-6, 0.016080841021827566,-0.024458388333600374, 0.026664997481476788,-0.024618364878703208, 0.020012524686320185,-0.014305251526745446, 0.008649943961959465,-0.003842008360761881, 308.6133633078010230E-6, 0.001852836963547170,-0.002797105957386612, 0.002832351511451895,-0.002320508982899863, 0.001594488333496738,-903.6623076669911820E-6};

static  float32_t*  mag_coeffs[ MAGNIFY_NUM ] =

{

        // for Index 0 [1xZoom == no zoom] the mag_coeffs will a NULL  ptr, since the filter is not going to be used in this  mode!
        (float32_t*)NULL,

        (float32_t*)(const float32_t[]){
            // 2x magnify - index 1
            // 12kHz, sample rate 48k, 60dB stopband, elliptic
            // a1 and coeffs[A2] negated! order: coeffs[B0], coeffs[B1], coeffs[B2], a1, coeffs[A2]
            // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
            0.228454526413293696,
            0.077639329099949764,
            0.228454526413293696,
            0.635534925142242080,
            -0.170083307068779194,

            0.436788292542003964,
            0.232307972937606161,
            0.436788292542003964,
            0.365885230717786780,
            -0.471769788739400842,

            0.535974654742658707,
            0.557035600464780845,
            0.535974654742658707,
            0.125740787233286133,
            -0.754725697183384336,

            0.501116342273565607,
            0.914877831284765408,
            0.501116342273565607,
            0.013862536615004284,
            -0.930973052446900984  },

            (float32_t*)(const float32_t[]){
                // 4x magnify - index 2
                // 6kHz, sample rate 48k, 60dB stopband, elliptic
                // a1 and coeffs[A2] negated! order: coeffs[B0], coeffs[B1], coeffs[B2], a1, coeffs[A2]
                // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
                0.182208761527446556,
                -0.222492493114674145,
                0.182208761527446556,
                1.326111070880959810,
                -0.468036100821178802,

                0.337123762652097259,
                -0.366352718812586853,
                0.337123762652097259,
                1.337053579516321200,
                -0.644948386007929031,

                0.336163175380826074,
                -0.199246162162897811,
                0.336163175380826074,
                1.354952684569386670,
                -0.828032873168141115,

                0.178588201750411041,
                0.207271695028067304,
                0.178588201750411041,
                1.386486967455699220,
                -0.950935065984588657  },

                (float32_t*)(const float32_t[]){
                    // 8x magnify - index 3
                    // 3kHz, sample rate 48k, 60dB stopband, elliptic
                    // a1 and coeffs[A2] negated! order: coeffs[B0], coeffs[B1], coeffs[B2], a1, coeffs[A2]
                    // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
                    0.185643392652478922,
                    -0.332064345389014803,
                    0.185643392652478922,
                    1.654637402827731090,
                    -0.693859842743674182,

                    0.327519300813245984,
                    -0.571358085216950418,
                    0.327519300813245984,
                    1.715375037176782860,
                    -0.799055553586324407,

                    0.283656142708241688,
                    -0.441088976843048652,
                    0.283656142708241688,
                    1.778230635987093860,
                    -0.904453944560528522,

                    0.079685368654848945,
                    -0.011231810140649204,
                    0.079685368654848945,
                    1.825046003243238070,
                    -0.973184930412286708  },

                    (float32_t*)(const float32_t[]){
                        // 16x magnify - index 4
                        // 1k5, sample rate 48k, 60dB stopband, elliptic
                        // a1 and coeffs[A2] negated! order: coeffs[B0], coeffs[B1], coeffs[B2], a1, coeffs[A2]
                        // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
                        0.194769868656866380,
                        -0.379098413160710079,
                        0.194769868656866380,
                        1.824436402073870810,
                        -0.834877726226893380,

                        0.333973874901496770,
                        -0.646106479315673776,
                        0.333973874901496770,
                        1.871892825636887640,
                        -0.893734096124207178,

                        0.272903880596429671,
                        -0.513507745397738469,
                        0.272903880596429671,
                        1.918161772571113750,
                        -0.950461788366234739,

                        0.053535383722369843,
                        -0.069683422367188122,
                        0.053535383722369843,
                        1.948900719896301760,
                        -0.986288064973853129 },

                        (float32_t*)(const float32_t[]){
                            // 32x magnify - index 5
                            // 750Hz, sample rate 48k, 60dB stopband, elliptic
                            // a1 and coeffs[A2] negated! order: coeffs[B0], coeffs[B1], coeffs[B2], a1, coeffs[A2]
                            // Iowa Hills IIR Filter Designer, DD4WH Aug 16th 2016
                            0.201507402588557594,
                            -0.400273615727755550,
                            0.201507402588557594,
                            1.910767558906650840,
                            -0.913508748356010480,

                            0.340295203367131205,
                            -0.674930558961690075,
                            0.340295203367131205,
                            1.939398230905991390,
                            -0.945058078678563840,

                            0.271859921641011359,
                            -0.535453706265515361,
                            0.271859921641011359,
                            1.966439529620203740,
                            -0.974705666636711099,

                            0.047026497485465592,
                            -0.084562104085501480,
                            0.047026497485465592,
                            1.983564238653704900,
                            -0.993055129539134551 }



};

//******* From here 2 set of filters for the I/Q FreeDV aliasing filter**********

// I- and Q- Filter instances for FreeDV downsampling aliasing filters

// N96K B4: FreeDV aliasing filters are used from AudioDriver_RxProcessorFreeDV() under USE_FREEDV,
// so they must be compiled in the opt-in FreeDV build although IS_SMALL_BUILD remains set.
#if !defined(IS_SMALL_BUILD) || defined(USE_FREEDV)


static arm_biquad_casd_df1_inst_f32 IIR_biquad_FreeDV_I =
{
        .numStages = 2,
        .pCoeffs = (float32_t *)(float32_t [])
        {
            1,0,0,0,0,  1,0,0,0,0 // passthru
        }, // 2 x 5 = 10 coefficients

        .pState = (float32_t *)(float32_t [])
        {
            0,0,0,0,   0,0,0,0
        } // 2 x 4 = 8 state variables
};



static arm_biquad_casd_df1_inst_f32 IIR_biquad_FreeDV_Q =
{
        .numStages = 2,
        .pCoeffs = (float32_t *)(float32_t [])
        {
            1,0,0,0,0,  1,0,0,0,0 // passthru
        }, // 2 x 5 = 10 coefficients

        .pState = (float32_t *)(float32_t [])
        {
            0,0,0,0,   0,0,0,0
        } // 2 x 4 = 8 state variables
};



static float32_t* FreeDV_coeffs[1] =
{
        (float32_t*)(const float32_t[])
		{
            // index 1
            // 2,4kHz, sample rate 48k, 50dB stopband, elliptic
            // only 2 stages!!!!!
            // a1 and coeffs[A2] negated! order: coeffs[B0], coeffs[B1], coeffs[B2], a1, coeffs[A2]
            // Iowa Hills IIR Filter Designer, DL2FW 20-10-16



            0.083165011486267731,
            -0.118387356334666696,
            0.083165011486267731,
            1.666027486884941840,
            -0.713970153522810569,

            0.068193683664968877,
            -0.007220581127135660,
            0.068193683664968877,
            1.763363677375461290,
            -0.892530463578263378
        }
};

#endif

//******* End of 2 set of filters for the I/Q FreeDV aliasing filter**********




// variables for FM squelch IIR filters
static float32_t	                 iir_squelch_rx_state [ IIR_RX_STATE_ARRAY_SIZE ];  // 44 = 32 + 12em ordre
static arm_iir_lattice_instance_f32	 IIR_Squelch_HPF;

// variables for TX IIR filter
float32_t		                iir_tx_State           [ IIR_RX_STATE_ARRAY_SIZE ];   // 44 = 32 + 12em ordre
arm_iir_lattice_instance_f32	IIR_HPF_TXFilter;
float32_t                       iir_tx_hpf_state            [ 32 + 4 ];   // 36 = 32 + 4em ordre NB: le filtre HPF 80 est de 4em ordre

float32_t                       iir_tx_lpf_compressor_State [ 32 + 4 ];   // 35 = 32 + 3em ordre
float32_t                       iir_tx_pass_tout   [8 + 12 ]; // filtre passe tout 137Hz  (9.25 ms)
float32_t                       iir_tx_pass_tout_2 [8 + 12 ]; // filtre passe tout 182Hz  (6.18 ms)
// float32_t                       iir_Rx_pass_tout   [8 +  8 ]; // filtre passe tout 278Hz  (3.25 ms)
// float32_t                    IIR_TX_Voice_Notch_state    [ 11  ];      // 8  + 2

// arm_iir_lattice_instance_f32	IIR_Rx_Interpo_48_to_96;
// float32_t   __MCHF_SPECIALMEM   iir_Rx_Interpo_48_to_96_State[ 64 + 3];     // 68 = 64 + 2em ordre

arm_iir_lattice_instance_f32	IIR_AntiAlias_48_to_96_I;
arm_iir_lattice_instance_f32	IIR_AntiAlias_48_to_96_Q;

float32_t   __MCHF_SPECIALMEM   iir_tx_I_48_to_96_State[ 64 + 4 + 1];     // 68 = 64 + 4em ordre
float32_t   __MCHF_SPECIALMEM   iir_tx_Q_48_to_96_State[ 64 + 4 + 1];     // 68 = 64 + 4em ordre


arm_iir_lattice_instance_f32	IIR_TX_LPF_Filter;
arm_iir_lattice_instance_f32	IIR_TX_PASS_TOUT_Filter;
arm_iir_lattice_instance_f32	IIR_TX_PASS_TOUT_Filter_2;
// arm_iir_lattice_instance_f32	IIR_RX_PASS_TOUT_Filter;
arm_iir_lattice_instance_f32	IIR_LPF_Compressor;


// N96K B4: FreeDV RX audio filter, needed by the opt-in FreeDV build (see AudioDriver_RxProcessorFreeDV).
#if !defined(IS_SMALL_BUILD) || defined(USE_FREEDV)      // pour reduire la memoire RAM static avec version small
arm_iir_lattice_instance_f32	IIR_FreeDV_RX_Filter;   // DL2FW: temporary installed FreeDV RX Audio Filter
float32_t		                iir_FreeDV_RX_State [ IIR_RX_STATE_ARRAY_SIZE ];   // 44 = 32 + 12em ordre
#endif



// S meter public
__IO SMeter					sm;

// ATTENTION: These data structures have been placed in CCM Memory (64k)
// IF THE SIZE OF  THE DATA STRUCTURE GROWS IT WILL QUICKLY BE OUT OF SPACE IN CCM
// Be careful! Check mchf-eclipse.map for current allocation

AudioDriverState   __MCHF_SPECIALMEM ads;
AudioDriverBuffer  __MCHF_SPECIALMEM adb;

#ifdef               USE_LMS_AUTONOTCH
LMSData            __MCHF_SPECIALMEM lmsData;
//LMSData            lmsData;
#endif


SnapCarrier   sc;

/**
 * @returns offset frequency in Hz for current frequency translate mode
 */

int32_t    AudioDriver_GetTranslateFreq()
{
    int32_t    fdelta = 0;
    switch (ts.iq_freq_mode)
    {
		case  FREQ_IQ_CONV_P6KHZ :	fdelta =   6000;	break;
		case  FREQ_IQ_CONV_M6KHZ :	fdelta = - 6000;	break;
		case  FREQ_IQ_CONV_P12KHZ:	fdelta =  12000;	break;
		case  FREQ_IQ_CONV_M12KHZ:	fdelta = -12000;	break;
    }
    return fdelta;
}

static  void  AudioDriver_InitFilters  (void);
        void  AudioDriver_SetupAgcWdsp (void);
//
// THE FOLLOWING FUNCTION HAS BEEN TESTED, BUT NOT USED - see the function "audio_rx_freq_conv"
//*----------------------------------------------------------------------------
//* Function Name       : audio_driver_config_nco
//* Object              :
//* Object              :
//* Input Parameters    :
//* Output Parameters   :
//* Functions called    :
//*----------------------------------------------------------------------------
/*

void  audio_driver_config_nco(void)
{
	// Configure NCO for the frequency translate function - NOT USED for the "Static" local oscillator!
	// see "audio_driver.h" for values

	ads.Osc_Cos     =  CONV_NCO_COS;
	ads.Osc_Sin     =  CONV_NCO_SIN;
	ads.Osc_Vect_Q  =  1;
	ads.Osc_Vect_I  =  0;
	ads.Osc_Gain    =  0;
	ads.Osc_Q       =  0;
	ads.Osc_I       =  0;
}
//
 */

void AudioDriver_Init(void)
{
    const  uint32_t  word_size =  WORD_SIZE_32;     ///  WORD_SIZE_16; // NIZ32

    // CW module init
    CwGen_Init();

    RttyDecoder_Init();
    PskDecoder_Init();

    // Audio filter disabled  and DSP processing
    ads.af_disabled = 1;
    ts.dsp_inhibit  = 1;


    // Reset S meter public
    sm.skip		    = 0;
    sm.s_count	    = 0;
    sm.curr_max	    = 0;
    sm.gain_calc    = 0;	        // gain calculation used for S-meter
#if 0
    ads.agc_val     = 1;			// Post AF Filter gain (AGC)
    ads.agc_var     = 1;			// used in AGC processing
    ads.agc_calc    = 1;			// used in AGC processing
    ads.agc_holder  = 1;			// initialize holder for AGC value

    for ( uint32_t x = 0;  x < BUFF_LEN;  x++ )	 // initialize running buffer for AGC delay
    {
        adb.agc_valbuf[x] = 1;       // a mettre IQ_BLOCK_SIZE aulieu de BUFF_LEN
    }
#endif
    ads.alc_val 					= 1;	// init TX audio auto-level-control (ALC)
    ads.fm_sql_avg                  = 0;	// init FM squelch averaging
    ads.fm_subaudible_tone_word     = 0;	// actively-used variable in producing the tone (0 disabled tone generation)
    ads.fm_tone_burst_word          = 0;	// this is the actively-used DDS tone word in the tone burst frequency generator
    ads.fm_subaudible_tone_det_freq = 0;	// frequency, in Hz, of currently-selected subaudible tone for detection
    ads.fm_subaudible_tone_gen_freq = 0;	// frequency, in Hz, of currently-selected subaudible tone for generation
    ads.beep_loudness_factor        = 0;	// scaling factor for beep loudness
    //
    ads.fm_tone_burst_active 		= 0;	// this is TRUE of the tone burst is actively being generated
    ads.fm_squelched 				= 0;	// TRUE if FM receiver audio is to be squelched
    ads.fm_subaudible_tone_detected = 0;	// TRUE if subaudible tone has been detected
    //
    ads.decimation_rate				=	RX_DECIMATION_RATE_12KHZ;		// 4 =  Decimation rate, when enabled

    //    ads.fade_leveler = 0;

    //
    //
//    AudioManagement_CalcAGCDecay();	// initialize AGC decay ("hang time") values
    //
//    AudioManagement_CalcRFGain();		// convert from user RF gain value to "working" RF gain value
    //
    AudioManagement_CalcALCDecay();	// initialize ALC decay values
    //
//    AudioManagement_CalcAGCVals();	// calculate AGC internal values from user settings
    //
//    AudioManagement_CalcNB_AGC();		// set up noise blanker AGC values
    //
    ts.cw_lsb = RadioManagement_CalculateCWSidebandMode();	// set up CW sideband mode setting
    //
    // The "active" NCO in the frequency translate function is NOT used, but rather a "static" sine that is an integer divisor of the sample rate.
    //
    //audio_driver_config_nco();	// Configure the NCO in the frequency translate function
    //
    ads.tx_filter_adjusting = 0;	// used to disable TX I/Q filter during adjustment
    // Audio Filter Init init
    AudioDriver_InitFilters();



    ts.codec_present = Codec_Reset ( ts.samp_rate_96k, word_size ) == HAL_OK;   // Lancement du codec  NIZ96

  //   non_os_delay(); //  non_os_delay(); //  non_os_delay();  // non_os_delay(); non_os_delay(); ///NIZZZ  tempo existant aussi sur la derniere version UHSDR



    // Audio filter enabled

   //  ts.dsp_inhibit   =  0;
   //  ads.af_disabled  =  0;

}

void AudioDriver_SetSamPllParameters()
{

    // definitions and intializations for synchronous AM demodulation = SAM
    //    adb.DF = 1.0; //ads.decimation_rate;
    adb.DF       = (float32_t) (ads.decimation_rate);
    //ads.pll_fmax_int = 2500;
    adb.pll_fmax = (float32_t) ads.pll_fmax_int;
    // DX adjustments: zeta = 0.15, omegaN = 100.0
    // very stable, but does not lock very fast
    // standard settings: zeta = 1.0, omegaN = 250.0
    // maybe user can choose between slow (DX), medium, fast SAM PLL
    // zeta / omegaN
    // DX = 0.2, 70
    // medium 0.6, 200
    // fast 1.0, 500
    //ads.zeta_int = 80; // zeta * 100 !!!
    // 0.01;// 0.001; // 0.1; //0.65; // PLL step response: smaller, slower response 1.0 - 0.1
    //ads.omegaN_int = 250; //200.0; // PLL bandwidth 50.0 - 1000.0
    adb.omegaN     = (float32_t)ads.omegaN_int;
    adb.zeta       = (float32_t)ads.zeta_int / 100.0;
    //pll
    adb.omega_min  = - (2.0 * PI * adb.pll_fmax * adb.DF / IQ_SAMPLE_RATE_F); //-0.5235987756; //
    adb.omega_max  =   (2.0 * PI * adb.pll_fmax * adb.DF / IQ_SAMPLE_RATE_F); //0.5235987756; //
    adb.g1         =   (1.0 - expf(-2.0 * adb.omegaN * adb.zeta * adb.DF / IQ_SAMPLE_RATE_F)); //0.0082987073611; //
    adb.g2         =   (- adb.g1 + 2.0 * (1 - expf(- adb.omegaN * adb.zeta * adb.DF / IQ_SAMPLE_RATE_F)
            		  * cosf(adb.omegaN * adb.DF / IQ_SAMPLE_RATE_F * sqrtf(1.0 - adb.zeta * adb.zeta)))); //0.01036367597097734813032783691644; //
    //fade leveler
    //    ads.tauR_int = 20; // -->  / 1000 = 0.02
    //    ads.tauI_int = 140; // --> / 100 = 1.4
    adb.tauR       = 0.02; // ((float32_t)ads.tauR_int) / 1000.0; //0.02; // original 0.02;
    adb.tauI       = 1.4; // ((float32_t)ads.tauI_int) / 100.0; //1.4; // original 1.4;
    adb.mtauR      = (expf(- adb.DF / (IQ_SAMPLE_RATE_F * adb.tauR))); //0.99948;
    adb.onem_mtauR = (1.0 - adb.mtauR);
    adb.mtauI      = (expf(- adb.DF / (IQ_SAMPLE_RATE_F * adb.tauI))); //0.99999255955;
    adb.onem_mtauI = (1.0 - adb.mtauI);
}


void   AudioDriver_SetRxAudioProcessingSAM ( uint8_t dmod_mode )
{
	// if (ts.paddles_active )  ts.audio_dac_output_muting_buffer_count += 30;    // pour muter le son Rx afin d'�conomiser le processeur qu'on a besoin ici
	    								      // et surtout � suspendre les filtres audios qu'on va changer de coeffs ici
	// for (int aaa=0; aaa < 10000; aaa++)  { } ; // delay pour faire passer le Rxprocessing en cours  avant de changer le mode et les coeff des filtres

    AudioDriver_SetSamPllParameters();

    //sideband separation, these values never change
    adb.c0[0] = -0.328201924180698;
    adb.c0[1] = -0.744171491539427;
    adb.c0[2] = -0.923022915444215;
    adb.c0[3] = -0.978490468768238;
    adb.c0[4] = -0.994128272402075;
    adb.c0[5] = -0.998458978159551;
    adb.c0[6] = -0.999790306259206;

    adb.c1[0] = -0.0991227952747244;
    adb.c1[1] = -0.565619728761389;
    adb.c1[2] = -0.857467122550052;
    adb.c1[3] = -0.959123933111275;
    adb.c1[4] = -0.988739372718090;
    adb.c1[5] = -0.996959189310611;
    adb.c1[6] = -0.999282492800792;

    // these change during operation
    adb.phase_corr = 0.0;
    adb.ampli_corr = 1.0;

 //   adb.teta1     = 0.0;
 //   adb.teta2     = 0.0;
 //   adb.teta3     = 0.0;

 //   adb.teta1_old = 0.0;
 //   adb.teta2_old = 0.0;
 //   adb.teta3_old = 0.0;
}



/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * Cascaded biquad (notch, peak, lowShelf, highShelf) [DD4WH, april 2016]
 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
// biquad_1 :   Notch & peak filters & lowShelf (Bass) in the decimated path
// biquad 2 :   Treble in the 48kHz path
// TX_biquad:   Bass & Treble in the 48kHz path
// DSP Audio-EQ-cookbook for generating the coeffs of the filters on the fly
// www.musicdsp.org/files/Audio-EQ-Cookbook.txt  [by Robert Bristow-Johnson]
//
// the ARM algorithm assumes the biquad form
// y[n] = coeffs[B0] * x[n] + coeffs[B1] * x[n-1] + coeffs[B2] * x[n-2] + coeffs[A1] * y[n-1] + a2 * y[n-2]
//
// However, the cookbook formulae by Robert Bristow-Johnson AND the Iowa Hills IIR Filter designer
// use this formula:
//
// y[n] = coeffs[B0] * x[n] + coeffs[B1] * x[n-1] + coeffs[B2] * x[n-2] - coeffs[A1] * y[n-1] - coeffs[A2] * y[n-2]
//
// Therefore, we have to use negated coeffs[A1] and coeffs[A2] for use with the ARM function
// notch implementation
//
// we also have to divide every coefficient by a0 !
// y[n] = coeffs[B0]/a0 * x[n] + coeffs[B1]/a0 * x[n-1] + coeffs[B2]/a0 * x[n-2] - coeffs[A1]/a0 * y[n-1] - coeffs[A2]/a0 * y[n-2]
//
//

#define B0 0
#define B1 1
#define B2 2
#define A1 3
#define A2 4

/**
 * @brief Biquad Filter Init Helper function which copies the biquad coefficients into the filter array itself
 */
void AudioDriver_SetBiquadCoeffs( float32_t* coeffsTo, const  float32_t*  coeffsFrom )
{
    coeffsTo[0] = coeffsFrom[0];
    coeffsTo[1] = coeffsFrom[1];
    coeffsTo[2] = coeffsFrom[2];
    coeffsTo[3] = coeffsFrom[3];
    coeffsTo[4] = coeffsFrom[4];
}

/**
 * @brief Biquad Filter Init Helper function which copies the biquad coefficients into all filter instances of the 1 or 2 channel audio
 * @param biquad_inst_array a pointer to an either 1 or 2 element sized array of biquad filter instances. Make sure the array has the expected size!
 * @param idx of first element of stage coefficients (i.e. 0, 5, 10, ... ) since we have 5 element coeff arrays per stage
 */
void AudioDriver_SetBiquadCoeffsAllInstances(arm_biquad_casd_df1_inst_f32 biquad_inst_array[NUM_AUDIO_CHANNELS], uint32_t idx, const float32_t* coeffsFrom)
{
    for ( int chan = 0; chan < NUM_AUDIO_CHANNELS; chan++ )
     {
         AudioDriver_SetBiquadCoeffs( &biquad_inst_array[chan].pCoeffs[idx], coeffsFrom );
     }
}

/**
 * @brief Biquad Filter Init Helper function which applies the filter specific scaling to calculated coefficients
 */
void AudioDriver_ScaleBiquadCoeffs( float32_t coeffs[5], const float32_t scalingA, const float32_t scalingB)
{
    coeffs[A1] = coeffs[A1] / scalingA;
    coeffs[A2] = coeffs[A2] / scalingA;

    coeffs[B0] = coeffs[B0] / scalingB;
    coeffs[B1] = coeffs[B1] / scalingB;
    coeffs[B2] = coeffs[B2] / scalingB;
}


/**
 * @brief Biquad Filter Init Helper function to calculate a notch filter aka narrow bandstop filter
 */


void  AudioDriver_CalcBandstop ( float32_t  coeffs[5], float32_t f0, float32_t  FS )
{
     float32_t   Q      =  10; // larger Q gives narrower notch
     float32_t   w0     =  2 * PI * f0 / FS;
     float32_t   alpha  =  sinf(w0) / (2 * Q);

     coeffs[B0]         = 1;
     coeffs[B1]         = - 2 * cosf(w0);
     coeffs[B2]         = 1;
     float32_t scaling  = 1 + alpha;
     coeffs[A1]         = 2 * cosf(w0); // already negated!
     coeffs[A2]         = alpha - 1;    // already negated!

     AudioDriver_ScaleBiquadCoeffs ( coeffs, scaling, scaling );
}

/**
 * @brief Biquad Filter Init Helper function to calculate a peak filter aka a narrow bandpass filter
 */

void AudioDriver_CalcBandpass( float32_t coeffs[5],  float32_t  f0,  float32_t   FS )
{
    /*  // peak filter = peaking EQ
    f0 = ts.peak_frequency;
    //Q = 15; //
    // bandwidth in octaves between midpoint (Gain / 2) gain frequencies
    float32_t BW = 0.05;
    w0 = 2 * PI * f0 / FSdec;
    //alpha = sin(w0) / (2 * Q);
    alpha = sin (w0) * sinh( log(2) / 2 * BW * w0 / sin(w0) );
    float32_t Gain = 12;
    A = powf(10.0, (Gain/40.0));
    coeffs[B0] = 1 + (alpha * A);
    coeffs[B1] = - 2 * cos(w0);
    coeffs[B2] = 1 - (alpha * A);
    a0 = 1 + (alpha / A);
    coeffs[A1] = 2 * cos(w0); // already negated!
    coeffs[A2] = (alpha/A) - 1; // already negated!
     */
    /*        // test the BPF coefficients, because actually we want a "peak" filter without gain!
    // Bandpass filter constant 0dB peak gain
    // this filter was tested: "should have more gain and less Q"
    f0 = ts.peak_frequency;
    Q = 20; //
    w0 = 2 * PI * f0 / FSdec;
    alpha = sin(w0) / (2 * Q);
//        A = 1; // gain = 1
    //        A = 3; // 10^(10/40); 15dB gain

    coeffs[B0] = alpha;
    coeffs[B1] = 0;
    coeffs[B2] = - alpha;
    a0 = 1 + alpha;
    coeffs[A1] = 2 * cos(w0); // already negated!
    coeffs[A2] = alpha - 1; // already negated!
     */
    // BPF: constant skirt gain, peak gain = Q

    float32_t  Q       =  4; //
    float32_t  BW      =  0.03;
    float32_t  w0      =  2 * PI * f0 / FS;
    float32_t  alpha   =  sinf (w0) * sinhf( log(2) / 2 * BW * w0 / sinf(w0) ); //
    float32_t  scaling =  1 + alpha;

    coeffs[B0]         =  Q * alpha;
    coeffs[B1]         =  0;
    coeffs[B2]         = -Q * alpha;
    coeffs[A1]         =  2 * cosf(w0);  // already negated!
    coeffs[A2]         =  alpha - 1;     // already negated!

    AudioDriver_ScaleBiquadCoeffs( coeffs, scaling,  scaling );

}

/**
 * @brief Biquad Filter Init Helper function to calculate a treble adjustment filter aka high shelf filter
 */
void AudioDriver_CalcHighShelf ( float32_t  coeffs[5],  float32_t  f0,  float32_t  S,  float32_t  gain,  float32_t  FS )
{
    float32_t w0       = 2.0 * PI * f0 / FS;
    float32_t A        = pow10f(gain/40.0); // gain ranges from -20 to 5
    float32_t alpha    = sinf(w0) / 2.0 * sqrtf( (A + 1.0/A) * (1.0/S - 1.0) + 2.0 );
    float32_t cosw0    = cosf(w0);
    float32_t twoAa    = 2.0 * sqrtf(A) * alpha;
    // highShelf
    //
    coeffs[B0]         =         A *  ( (A + 1.0) + (A - 1.0) * cosw0 + twoAa );
    coeffs[B1]         = - 2.0 * A *  ( (A - 1.0) + (A + 1.0) * cosw0         );
    coeffs[B2]         =         A *  ( (A + 1.0) + (A - 1.0) * cosw0 - twoAa );
    float32_t scaling  =                (A + 1.0) - (A - 1.0) * cosw0 + twoAa ;
    coeffs[A1]         = - 2.0 *      ( (A - 1.0) - (A + 1.0) * cosw0         ); // already negated!
    coeffs[A2]         = twoAa      -   (A + 1.0) + (A - 1.0) * cosw0; // already negated!


    //    DCgain = 2; //
    //    DCgain = (coeffs[B0] + coeffs[B1] + coeffs[B2]) / (1 - (- coeffs[A1] - coeffs[A2])); // takes into account that coeffs[A1] and coeffs[A2] are already negated!
    float32_t DCgain  = 1.0 * scaling;

    AudioDriver_ScaleBiquadCoeffs( coeffs, scaling, DCgain );
}

/**
 * @brief Biquad Filter Init Helper function to calculate a bass adjustment filter aka low shelf filter
 */
void AudioDriver_CalcLowShelf ( float32_t coeffs[5], float32_t f0, float32_t Sharp, float32_t gain, float32_t FS )
{
    float32_t   w0     = 2.0 * PI * f0 / FS;
    float32_t   A      = pow10f(gain/40.0);      // gain ranges from -20 to 5
    float32_t  alpha   = sinf(w0) / 2.0 * sqrtf( (A + 1.0/A) * (1.0/Sharp - 1.0) + 2.0 );
    float32_t  cosw0   = cosf(w0);
    float32_t  twoAa   = 2.0 * sqrtf(A) * alpha;
  // lowShelf
    coeffs[B0]         =       A *    ( (A + 1.0) - (A - 1.0) * cosw0 + twoAa );
    coeffs[B1]         = 2.0 * A *    ( (A - 1.0) - (A + 1.0) * cosw0         );
    coeffs[B2]         =       A *    ( (A + 1.0) - (A - 1.0) * cosw0 - twoAa );
    float32_t  scaling =                (A + 1.0) + (A - 1.0) * cosw0 + twoAa ;
    coeffs[A1]         = 2.0 *        ( (A - 1.0) + (A + 1.0) * cosw0         );   // already negated!
    coeffs[A2]         = twoAa        - (A + 1.0) - (A - 1.0) * cosw0;             // already negated!

    // scaling the feedforward coefficients for gain adjustment !
    // "DC gain of an IIR filter is the sum of the filters� feedforward coeffs divided by
    // 1 minus the sum of the filters� feedback coeffs" (Lyons 2011)
    //    float32_t DCgain = (coeffs[B0] + coeffs[B1] + coeffs[B2]) / (1 - (coeffs[A1] + coeffs[A2]));
    // does not work for some reason?
    // I take a divide by a constant instead !
    //    DCgain = (coeffs[B0] + coeffs[B1] + coeffs[B2]) / (1 - (- coeffs[A1] - coeffs[A2])); // takes into account that coeffs[A1] and coeffs[A2] are already negated!

    float32_t   DCgain = 1.0 * scaling; //
    AudioDriver_ScaleBiquadCoeffs ( coeffs, scaling, DCgain );
}

/**
 * @brief Biquad Filter Init Helper function to calculate a notch filter aka narrow bandstop filter with variable bandwidth
 */


void  AudioDriver_CalcNotch ( float32_t coeffs[5], float32_t f0, float32_t BW, float32_t FS )
{

    float32_t  w0     = 2 * PI * f0 / FS;
    float32_t  alpha  = sinf(w0)*sinh( logf(2.0)/2.0 * BW * w0/sinf(w0) );

    coeffs[B0]        = 1;
    coeffs[B1]        = - 2 * cosf(w0);
    coeffs[B2]        = 1;
    float32_t scaling = 1 + alpha;
    coeffs[A1]        = 2 * cosf(w0); // already negated!
    coeffs[A2]        = alpha - 1; // already negated!


    AudioDriver_ScaleBiquadCoeffs ( coeffs, scaling, scaling );
}


//***************************************************************************************************************


void   AudioDriver_Calc_medium_Equalizer ( float32_t  coeffs[5],  float32_t  f0, float32_t  Q, float32_t gainDB, float32_t  FS )
{
   //**************************************************************************************************
   //*******************  Calcul des coeff d'un egualiseur Biquad en medium (Nizar) Fev 2024 **********
   //**************************************************************************************************

    double  A          =  pow10f(gainDB / 40.0 );     // Note : gainDB / 40 car c'est un filtre peak/notch
    double  w0         =  2.0 * PI * f0 / FS;         // Calculer la fréquence normalisée
    double  cosw0      =  cos(w0);
    double  sinw0      =  sin(w0);
    double  alpha      =  sinw0 / (2.0 * Q);          // le Q na plus le meme sens que des basses ou des trebles, a revoir le coeff
    double  a0         =  1.0 + alpha / A;            // scaling

			coeffs[B0] =  ( 1.0 + alpha * A ) / a0;
			coeffs[B1] =  (-2.0 * cosw0     ) / a0;
			coeffs[B2] =  ( 1.0 - alpha * A ) / a0;
			coeffs[A1] =  ( 2.0 * cosw0     ) / a0;  // A1 et A2 sont a signe inversés par rapport a ceux du Mathlab
			coeffs[A2] =  (-1.0 + alpha / A ) / a0;
}


//***********************************************************************************************************


//  static const float32_t biquad_passthrough[] = { 1, 0, 0, 0, 0 };

//********************************************************************************************************

void   AudioDriver_Set_Tx_Audio_Equalizer()
{
	float32_t  coeffs[5];

	    float32_t  Fsync = ts.tx_decimatedIQ ? 12000.0 : 48000.0;  // at decimated 12Khz for ( (USB + LSB) & not_DIG_IQ )

	    float32_t  bass_freq   = 75;   //  280Hz =  130 +  75 * 2
	    float32_t  medium_freq = 100;  //  900Hz =  400 + 100 * 5
	    float32_t  trebl_freq  = 255;  // 2750Hz = 1475 + 255 * 5

	    float32_t  bass_gain, medium_gain , trebl_gain ;

	    float32_t  bass_BW    = 99.8; // 0.1 % = (100 - 9908)/2 ; le 99  correspond au 0.5%
	    float32_t  medium_BW  = 85.0; // 0.15 = 10 / ( 100 -85);
	    float32_t  trebl_BW   = 70.0; // 0.30 = 10 / (100 -70) ;
	    /*
	    switch  ( ts.tx_equalizer )
		{
			case 0  :   bass_freq   = ts.tx_bass_filter_freq;    bass_gain   = ts.tx_bass_gain;           bass_BW   = ts.tx_bass_filter_bwidth;
			            medium_freq = ts.tx_medium_filter_freq;  medium_gain = ts.tx_medium_filter_gain;  medium_BW = ts.tx_medium_filter_bwidth;
			            trebl_freq  = ts.tx_treb_filter_freq;    trebl_gain  = ts.tx_treble_gain;         trebl_BW  = ts.tx_treb_filter_bwidth;
			            break;
			case 1  :  	bass_gain   = 10.5; medium_gain = 21.2;	trebl_gain  = 32.2;	break;  // "Trebl7"
			case 2  :  	bass_gain   = 9.8;  medium_gain = 19.8;	trebl_gain  = 30.2;	break;  // "Trebl6"
			case 3  :  	bass_gain   = 8.7;  medium_gain = 17.6;	trebl_gain  = 27.6;	break;  // "Trebl5"
			case 4  :  	bass_gain   = 6.5;  medium_gain = 13.2;	trebl_gain  = 21.4;	break;  // "Trebl4"
			case 5  :	bass_gain   = 6.0;  medium_gain = 12.2;	trebl_gain  = 20.0;	break;  // "Trebl3"
			case 6  : 	bass_gain   = 5.4;	medium_gain = 11.0;	trebl_gain  = 18.0;	break;  // "Trebl2"
			case 7  : 	bass_gain   = 4.9;	medium_gain = 10.0;	trebl_gain  = 16.4;	break;  // "Trebl1"
			case 8  : 	bass_gain   = 4.2;	medium_gain = 8.6;	trebl_gain  = 14.0;	break;  // "Mdium3"
			case 9  : 	bass_gain   = 3.5;	medium_gain = 7.0;	trebl_gain  = 12.0;	break;  // "Mdium2"
			case 10 :	bass_gain   = 2.9;	medium_gain = 5.8;	trebl_gain  = 10.0;	break;  // "Mdium1"
			case 11 :  	bass_gain   = 2.4;	medium_gain = 5.0;	trebl_gain  = 8.0;	break;  // " Flat4"
			case 12 : 	bass_gain   = 1.8;	medium_gain = 3.6;	trebl_gain  = 6.0;	break;  // " Flat3"
			case 13 : 	bass_gain   = 1.2;	medium_gain = 2.4;  trebl_gain  = 4.0;	break;  // " Flat2"
			case 14 : 	bass_gain   = 0.5;  medium_gain = 1.0;	trebl_gain  = 2.0;	break;  // " Flat1"
		} */


	    /*
	    switch  ( ts.tx_equalizer )   // extreme basse au peu plus accentuée
		{
			case 0  :   bass_freq   = ts.tx_bass_filter_freq;    bass_gain   = ts.tx_bass_gain;           bass_BW   = ts.tx_bass_filter_bwidth;
						medium_freq = ts.tx_medium_filter_freq;  medium_gain = ts.tx_medium_filter_gain;  medium_BW = ts.tx_medium_filter_bwidth;
						trebl_freq  = ts.tx_treb_filter_freq;    trebl_gain  = ts.tx_treble_gain;         trebl_BW  = ts.tx_treb_filter_bwidth;
						break;
			case 1  :  	bass_gain   = 10.8; medium_gain = 21.2;	trebl_gain  = 32.2;	break;  // "Trebl7"
			case 2  :  	bass_gain   = 10.1; medium_gain = 19.8;	trebl_gain  = 30.2;	break;  // "Trebl6"
			case 3  :  	bass_gain   = 9.0;  medium_gain = 17.6;	trebl_gain  = 27.6;	break;  // "Trebl5"
			case 4  :  	bass_gain   = 6.8;  medium_gain = 13.2;	trebl_gain  = 21.4;	break;  // "Trebl4"
			case 5  :	bass_gain   = 6.3;  medium_gain = 12.2;	trebl_gain  = 20.0;	break;  // "Trebl3"
			case 6  : 	bass_gain   = 5.7;	medium_gain = 11.0;	trebl_gain  = 18.0;	break;  // "Trebl2"
			case 7  : 	bass_gain   = 5.2;	medium_gain = 10.0;	trebl_gain  = 16.4;	break;  // "Trebl1"
			case 8  : 	bass_gain   = 4.5;	medium_gain = 8.6;	trebl_gain  = 14.0;	break;  // "Mdium3"
			case 9  : 	bass_gain   = 3.8;	medium_gain = 7.0;	trebl_gain  = 12.0;	break;  // "Mdium2"
			case 10 :	bass_gain   = 3.2;	medium_gain = 5.8;	trebl_gain  = 10.0;	break;  // "Mdium1"
			case 11 :  	bass_gain   = 2.7;	medium_gain = 5.0;	trebl_gain  = 8.0;	break;  // " Flat4"
			case 12 : 	bass_gain   = 2.1;	medium_gain = 3.6;	trebl_gain  = 6.0;	break;  // " Flat3"
			case 13 : 	bass_gain   = 1.5;	medium_gain = 2.4;  trebl_gain  = 4.0;	break;  // " Flat2"
			case 14 : 	bass_gain   = 0.8;  medium_gain = 1.0;	trebl_gain  = 2.0;	break;  // " Flat1"
		}
     */

	    /*
	    switch  ( ts.tx_equalizer )   // extreme basse au peu plus accentuée, legere réduction des medium
		{
			case 0  :   bass_freq   = ts.tx_bass_filter_freq;    bass_gain   = ts.tx_bass_gain;           bass_BW   = ts.tx_bass_filter_bwidth;
						medium_freq = ts.tx_medium_filter_freq;  medium_gain = ts.tx_medium_filter_gain;  medium_BW = ts.tx_medium_filter_bwidth;
						trebl_freq  = ts.tx_treb_filter_freq;    trebl_gain  = ts.tx_treble_gain;         trebl_BW  = ts.tx_treb_filter_bwidth;
						break;
			case 1  :  	bass_gain   = 10.9; medium_gain = 19.4;	trebl_gain  = 32.8;	break;  // "Trebl7"
			case 2  :  	bass_gain   = 10.4; medium_gain = 19.4;	trebl_gain  = 29.8;	break;  // "Trebl6"
			case 3  :  	bass_gain   = 9.0;  medium_gain = 17.2;	trebl_gain  = 27.6;	break;  // "Trebl5"
			case 4  :  	bass_gain   = 6.8;  medium_gain = 12.8;	trebl_gain  = 21.4;	break;  // "Trebl4"
			case 5  :	bass_gain   = 6.3;  medium_gain = 11.8;	trebl_gain  = 20.0;	break;  // "Trebl3"
			case 6  : 	bass_gain   = 5.7;	medium_gain = 10.6;	trebl_gain  = 18.0;	break;  // "Trebl2"
			case 7  : 	bass_gain   = 5.2;	medium_gain =  9.6;	trebl_gain  = 16.4;	break;  // "Trebl1"
			case 8  : 	bass_gain   = 4.5;	medium_gain = 8.2;	trebl_gain  = 14.0;	break;  // "Mdium3"
			case 9  : 	bass_gain   = 3.8;	medium_gain = 7.0;	trebl_gain  = 12.0;	break;  // "Mdium2"
			case 10 :	bass_gain   = 3.2;	medium_gain = 5.8;	trebl_gain  = 10.0;	break;  // "Mdium1"
			case 11 :  	bass_gain   = 2.7;	medium_gain = 5.0;	trebl_gain  = 8.0;	break;  // " Flat4"
			case 12 : 	bass_gain   = 2.1;	medium_gain = 3.6;	trebl_gain  = 6.0;	break;  // " Flat3"
			case 13 : 	bass_gain   = 1.5;	medium_gain = 2.4;  trebl_gain  = 4.0;	break;  // " Flat2"
			case 14 : 	bass_gain   = 0.8;  medium_gain = 1.0;	trebl_gain  = 2.0;	break;  // " Flat1"
		}*/


	    switch  ( ts.tx_equalizer )   // extreme basse au peu plus accentuée, legere réduction des medium
		{
			case 0  :   bass_freq   = ts.tx_bass_filter_freq;    bass_gain   = ts.tx_bass_gain;           bass_BW   = ts.tx_bass_filter_bwidth;
						medium_freq = ts.tx_medium_filter_freq;  medium_gain = ts.tx_medium_filter_gain;  medium_BW = ts.tx_medium_filter_bwidth;
						trebl_freq  = ts.tx_treb_filter_freq;    trebl_gain  = ts.tx_treble_gain;         trebl_BW  = ts.tx_treb_filter_bwidth;
						break;
			case 1  :  	bass_gain   = 10.2; medium_gain = 19.4;	trebl_gain  = 37.8;	break;  // "Trebl7"
			case 2  :  	bass_gain   = 10.9; medium_gain = 19.4;	trebl_gain  = 33.8;	break;  // "Trebl6"
			case 3  :  	bass_gain   = 10.3; medium_gain = 19.4;	trebl_gain  = 30.2;	break;  // "Trebl5"
			case 4  :  	bass_gain   = 8.9;  medium_gain = 17.2;	trebl_gain  = 28.0;	break;  // "Trebl4"
			case 5  :  	bass_gain   = 6.7;  medium_gain = 12.8;	trebl_gain  = 22.0;	break;  // "Trebl3"
			case 6  :	bass_gain   = 6.2;  medium_gain = 11.8;	trebl_gain  = 20.4;	break;  // "Trebl2"
			case 7  : 	bass_gain   = 5.6;	medium_gain = 10.6;	trebl_gain  = 18.2;	break;  // "Trebl1"
			case 8  : 	bass_gain   = 5.1;	medium_gain =  9.6;	trebl_gain  = 16.6;	break;  // "Trebl1"
			case 9  : 	bass_gain   = 4.4;	medium_gain = 8.2;	trebl_gain  = 14.0;	break;  // "Mdium3"
			case 10 : 	bass_gain   = 3.7;	medium_gain = 7.0;	trebl_gain  = 12.0;	break;  // "Mdium2"
			case 11 :	bass_gain   = 3.1;	medium_gain = 5.8;	trebl_gain  = 10.0;	break;  // "Mdium1"
			case 12 :  	bass_gain   = 2.6;	medium_gain = 5.0;	trebl_gain  = 8.0;	break;  // " Flat4"
			case 13 : 	bass_gain   = 2.0;	medium_gain = 3.6;	trebl_gain  = 6.0;	break;  // " Flat3"
			case 14 : 	bass_gain   = 1.4;	medium_gain = 2.4;  trebl_gain  = 4.0;	break;  // " Flat2"
		//	case 14 : 	bass_gain   = 0.7;  medium_gain = 1.0;	trebl_gain  = 2.0;	break;  // " Flat1"
		}

/*
	    switch( ts.tx_equalizer )     // les extremes aigues legerement plus accentuées et extreme basse légerement plus tardive en temps de reponse
		{
			case 0  :   bass_freq   = ts.tx_bass_filter_freq;    bass_gain   = ts.tx_bass_gain;           bass_BW   = ts.tx_bass_filter_bwidth;
						medium_freq = ts.tx_medium_filter_freq;  medium_gain = ts.tx_medium_filter_gain;  medium_BW = ts.tx_medium_filter_bwidth;
						trebl_freq  = ts.tx_treb_filter_freq;    trebl_gain  = ts.tx_treble_gain;         trebl_BW  = ts.tx_treb_filter_bwidth;
						break;
			case 1  :  	bass_gain   = 10.5; medium_gain = 19.0;	trebl_gain  = 32.2;	break;  // "Trebl7"
			case 2  :  	bass_gain   = 9.8;  medium_gain = 18.2;	trebl_gain  = 30.2;	break;  // "Trebl6"
			case 3  :  	bass_gain   = 8.7;  medium_gain = 16.4;	trebl_gain  = 27.6;	break;  // "Trebl5"
			case 4  :  	bass_gain   = 6.5;  medium_gain = 11.6;	trebl_gain  = 21.4;	break;  // "Trebl4"
			case 5  :	bass_gain   = 6.0;  medium_gain = 10.6;	trebl_gain  = 20.0;	break;  // "Trebl3"
			case 6  : 	bass_gain   = 5.4;	medium_gain =  9.8;	trebl_gain  = 18.0;	break;  // "Trebl2"
			case 7  : 	bass_gain   = 4.9;	medium_gain =  8.8;	trebl_gain  = 16.4;	break;  // "Trebl1"
			case 8  : 	bass_gain   = 4.2;	medium_gain = 7.6;	trebl_gain  = 14.0;	break;  // "Mdium3"
			case 9  : 	bass_gain   = 3.5;	medium_gain = 6.2;	trebl_gain  = 12.0;	break;  // "Mdium2"
			case 10 :	bass_gain   = 2.9;	medium_gain = 5.8;	trebl_gain  = 10.0;	break;  // "Mdium1"
			case 11 :  	bass_gain   = 2.4;	medium_gain = 5.0;	trebl_gain  = 8.0;	break;  // " Flat4"
			case 12 : 	bass_gain   = 1.8;	medium_gain = 3.6;	trebl_gain  = 6.0;	break;  // " Flat3"
			case 13 : 	bass_gain   = 1.2;	medium_gain = 2.4;  trebl_gain  = 4.0;	break;  // " Flat2"
			case 14 : 	bass_gain   = 0.5;  medium_gain = 1.0;	trebl_gain  = 2.0;	break;  // " Flat1"
		}


*/
	                    //  Tx egualiseur Treeble (at 12Khz SSB) ********************************
	    AudioDriver_Calc_medium_Equalizer ( coeffs, 1475.0 + trebl_freq * 5.0,   10.0/(100.0 - trebl_BW) , trebl_gain/2.0, Fsync );
//	    AudioDriver_CalcHighShelf         ( coeffs, 1475.0 + ((float32_t) ts.tx_treb_filter_freq) * 5.0,                           ((float32_t)ts.tx_treb_filter_bwidth)/100.0, (float32_t)ts.tx_treble_gain, Fsync);  // 2000// 1700
	    AudioDriver_SetBiquadCoeffs ( &IIR_TX_biquad.pCoeffs[0], coeffs );


	                    //  TX  Medium Filter equalizer  ( at 48Khz  and 12Khz sample rate for USB & LSB )
	    AudioDriver_Calc_medium_Equalizer ( coeffs, 400.0 + medium_freq * 5.0,   10.0/(100.0 - medium_BW), medium_gain/2.0 , Fsync );
     // AudioDriver_CalcHighShelf         ( coeffs, 400.0 + (float32_t) ts.tx_medium_filter_freq * 5.0, ((float32_t) ts.tx_medium_filter_bwidth)/100.0, (float32_t)ts.tx_medium_filter_gain ,   Fsync);  // 2000// 1700
		AudioDriver_SetBiquadCoeffs ( &IIR_TX_biquad.pCoeffs[5], coeffs );


	                    // the TX bass filter is in IIR_TX_biquad  ( at 48Khz  and 12Khz sample rate for USB & LSB )
	    AudioDriver_CalcLowShelf ( coeffs,   130 +  bass_freq * 2.0,    bass_BW/200.0,    bass_gain,    Fsync);      // 200 // 300   0.7
	    AudioDriver_SetBiquadCoeffs ( &IIR_TX_biquad.pCoeffs[10], coeffs );
}

//****************************************************************************************************************

void AudioDriver_Set_Rx_Audio_Equalizer()
{
	ads.af_disabled++;
	ts.dsp_inhibit++;



	float32_t  coeffs[5];
	float32_t  FSdec = 24000.0; // we need the sampling rate in the decimated path for calculation of the coefficients

	if ( FilterPathInfo[ ts.filter_path ].sample_rate_dec == RX_DECIMATION_RATE_12KHZ )  // SSB , CW
	{
		FSdec = 12000.0;
	}

	const  float32_t  FS = IQ_SAMPLE_RATE; // we need this for the treble filter
				// RX EQ shelving filters
				// the bass filter is in biquad 1 and works at the decimated sample rate FSdec  12khz
				// Rx Bass / lowShelf
	AudioDriver_CalcLowShelf ( coeffs, 150.0 + (float32_t)ts.rx_bass_filter_freq, ((float32_t) ts.rx_bass_filter_bwidth)/100.0, ts.bass_gain, FSdec );     // 250,  Fsdec = 12Khz 0.7
	AudioDriver_SetBiquadCoeffsAllInstances ( &IIR_biquad_Rx_Equaliser_Bass_decim, 0, coeffs );

	// Treble = highShelf
		// the Rx treble filter is in biquad 2 and works at decimated  12kHz
	AudioDriver_Calc_medium_Equalizer ( coeffs, ((float32_t)ts.rx_trebl_filter_freq) * 10.0 + 400.0,   10.0/( 100.0 - (float32_t) ts.rx_trebl_filter_bwidth ), ts.treble_gain , FSdec );
//	AudioDriver_CalcHighShelf         ( coeffs, ((float32_t)ts.rx_trebl_filter_freq) * 10.0 + 400.0, ((float32_t) ts.rx_trebl_filter_bwidth)/100.0, ts.treble_gain, FSdec );    // 3500     FSdec=12Khz  0.9
	AudioDriver_SetBiquadCoeffsAllInstances ( &IIR_biquad_Rx_Treble_decim, 0, coeffs );

				// Treble = highShelf
				// the Rx treble filter is in biquad 2 and works at 48kHz
	AudioDriver_CalcHighShelf ( coeffs, ((float32_t)ts.rx_trebl_filter_freq) * 10.0 + 400.0, ((float32_t) ts.rx_trebl_filter_bwidth)/100.0, ts.treble_gain, FS );    // 3500     FS=48Khz  0.9
	AudioDriver_SetBiquadCoeffsAllInstances ( &IIR_biquad_Rx_Equaliser_Treble_48Khz, 0, coeffs );

	 	 	 	// Unlock - re-enable filtering
	if  ( ts.dsp_inhibit  ) {  ts.dsp_inhibit--;  }
	if  ( ads.af_disabled ) {  ads.af_disabled--; }

}



/**
 * @brief Biquad Filter Init used for processing audio for RX and TX
 */
void AudioDriver_SetRxTxAudioProcessingAudioFilters()
{
	 AudioDriver_Set_Rx_Audio_Equalizer();
						// coefficient calculation for TX treble adjustment
						// the TX treble filter is in IIR_TX_biquad and works at 48khz SR ou decimated 12Khz for Microphone voice SSB mode
	 AudioDriver_Set_Tx_Audio_Equalizer();

    float32_t FSdec = 24000.0; // we need the sampling rate in the decimated path for calculation of the coefficients

    if ( FilterPathInfo[ts.filter_path].sample_rate_dec == RX_DECIMATION_RATE_12KHZ )  // SSB , CW
    {
        FSdec = 12000.0;
    }

  //  const float32_t FS = IQ_SAMPLE_RATE; // we need this for the treble filter

    // the notch filter is in biquad 1 and works at the decimated sample rate FSdec

    float32_t    coeffs[5];

    // setting the Coefficients in the notch filter instance

    AudioDriver_CalcBandstop ( coeffs, ts.notch_frequency , FSdec );
    AudioDriver_SetBiquadCoeffsAllInstances ( &IIR_biquad_Rx_Manuel_Notch_decim, 0, coeffs );

					// this is an auto-notch-filter detected by the NR algorithm
					// biquad 1, 4th stage
					//    if(0) // temporarily deactivated
					//    if((ts.dsp_active & DSP_NOTCH_ENABLE) && (FilterPathInfo[ts.filter_path].sample_rate_dec == RX_DECIMATION_RATE_12KHZ))



    				// the peak filter is in biquad 1 and works at the decimated sample rate FSdec

    AudioDriver_CalcBandpass ( coeffs, ts.peak_frequency, FSdec );
    AudioDriver_SetBiquadCoeffsAllInstances ( &IIR_biquad_Rx_Manuel_Peak_decim, 0, coeffs );

}


/**
 * @brief configures filters/dsp etc. so that audio processing works according to the current configuration
 * @param dmod_mode needs to know the demodulation mode
 * @param reset_dsp_nr whether it is supposed to reset also DSP related filters (in most cases false is to be used here)
 */
//*****************************************************************************************************************************
//*****************************************************************************************************************************

void AudioDriver_SetRxAudioProcessing ( uint8_t  dmod_mode,  bool  reset_dsp_nr )
{
    // WARNING:  You CANNOT reliably use the built-in IIR and FIR "init" functions when using CONST-based coefficient tables!  If you do so, you risk filters
    //  not initializing properly!  If you use the "init" functions, you MUST copy CONST-based coefficient tables to RAM first!
    //  This information is from recommendations by online references for using ARM math/DSP functions
	ads.af_disabled++;
	ts.dsp_inhibit++;

#ifdef USE_ALTERNATE_NR
    // N96K A2: backport of the AudioNr_Prepare() call site of upstream df9ef3f9 (#1749). Upstream
    // calls it from RadioManagement_SetDemodMode() when DEMOD_DIGI is left. In this firmware that
    // function calls us BEFORE ts.dmod_mode is updated, so the old mode is still visible here and
    // this is the equivalent hook (the only mode change path). Leaving a digital mode means FreeDV
    // may have used the NR buffers (same MultiModeBuffer union): drop the residue, otherwise the
    // NR audio filter chain can be fed FreeDV data (and NaNs) -> "silence after FreeDV + NR" (#1749).
    if ( (ts.dmod_mode == DEMOD_DIGI) && (dmod_mode != DEMOD_DIGI) )
    {
        AudioNr_Prepare();
    }

    // N96K B3: also restart the NR pipeline whenever NR is switched on again (audit R9/A4). The
    // previous enable state is tracked in audio_nr.c because not every UI path passes
    // reset_dsp_nr = true (e.g. the DSP enable toggle recalls ts.dsp_active with false).
    AudioNr_UpdateEnableState((ts.dsp_active & DSP_NR_ENABLE) != 0);
#endif

    // make sure we have a proper filter path for the given mode

    // the commented out part made the code  only look at last used/selected filter path if the current filter path is not applicable
    // with it commented out the filter path is ALWAYS loaded from the last used/selected memory
    // I.e. setting the ts.filter_path anywere else in the code is useless. You have to call AudioFilter_NextApplicableFilterPath in order to
    // select a new filter path as this sets the last used/selected memory for a demod mode.

    ts.filter_path = AudioFilter_NextApplicableFilterPath ( PATH_ALL_APPLICABLE | PATH_LAST_USED_IN_MODE, AudioFilter_GetFilterModeFromDemodMode ( dmod_mode ), ts.filter_path );

    for (int chan = 0; chan < NUM_AUDIO_CHANNELS; chan++)
    {
        if (FilterPathInfo[ts.filter_path].pre_instance != NULL)
        {
            				// if we turn on a filter, set the number of members to the number of elements last
            IIR_PreFilter [ chan ].pkCoeffs  = FilterPathInfo[ ts.filter_path ].pre_instance->pkCoeffs;  // point to reflection coefficients
            IIR_PreFilter [ chan ].pvCoeffs  = FilterPathInfo[ ts.filter_path ].pre_instance->pvCoeffs;  // point to ladder coefficients
            IIR_PreFilter [ chan ].numStages = FilterPathInfo[ ts.filter_path ].pre_instance->numStages; // number of stages
        }
        else
        {
            			// if we turn off a filter, set the number of members to 0 first
            IIR_PreFilter [ chan ].numStages =  0;        // number of stages
            IIR_PreFilter [ chan ].pkCoeffs  =  NULL; // point to reflection coefficients
            IIR_PreFilter [ chan ].pvCoeffs  =  NULL; // point to ladder coefficients
        }

        			// Initialize IIR filter state buffer
        arm_fill_f32 ( 0.0, iir_rx_State [ chan ], IIR_RX_STATE_ARRAY_SIZE );

        IIR_PreFilter[chan].pState = iir_rx_State[ chan ];					// point to state array for IIR filter

        			// Initialize IIR antialias filter state buffer
        if (FilterPathInfo[ts.filter_path].iir_instance != NULL)
        {
            // if we turn on a filter, set the number of members to the number of elements last
            IIR_AntiAlias[chan].pkCoeffs   = FilterPathInfo[ts.filter_path].iir_instance->pkCoeffs;  // point to reflection coefficients
            IIR_AntiAlias[chan].pvCoeffs   = FilterPathInfo[ts.filter_path].iir_instance->pvCoeffs;  // point to ladder coefficients
            IIR_AntiAlias[chan].numStages  = FilterPathInfo[ts.filter_path].iir_instance->numStages; // number of stages
        }
        else
        {
            // if we turn off a filter, set the number of members to 0 first
            IIR_AntiAlias[chan].numStages = 0;
            IIR_AntiAlias[chan].pkCoeffs  = NULL;
            IIR_AntiAlias[chan].pvCoeffs  = NULL;
        }

        arm_fill_f32 ( 0.0, iir_aa_State[chan], IIR_RX_STATE_ARRAY_SIZE );
        IIR_AntiAlias[chan].pState = iir_aa_State[chan];					// point to state array for IIR filter
    }

			// TODO: We only have to do this, if the audio signal filter configuration changes
			// RX+ TX Bass, Treble, Peak, Notch
    AudioDriver_SetRxTxAudioProcessingAudioFilters();

    			// initialize the goertzel filter used to detect CW signals at a given frequency in the audio stream
    CwDecode_FilterInit();

					// this sets the coefficients for the ZoomFFT decimation filter
					// according to the desired magnification mode sd.magnify
					// sd.magnify 0 = 1x magnification
					// sd.magnify 1 = 2x
					// sd.magnify 2 = 4x
					// sd.magnify 3 = 8x
					// sd.magnify 4 = 16x
					// sd.magnify 5 = 32x
					//
					// for 0 the mag_coeffs will a NULL  ptr, since the filter is not going to be used in this  mode!
    IIR_biquad_Zoom_FFT_I.pCoeffs = mag_coeffs [ sd.magnify ];
    IIR_biquad_Zoom_FFT_Q.pCoeffs = mag_coeffs [ sd.magnify ];

					/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
					 * End of coefficient calculation and setting for cascaded biquad
					 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
					//
					// Initialize high-pass filter used for the FM noise squelch
					//
    IIR_Squelch_HPF.pkCoeffs  =  IIR_15k_hpf.pkCoeffs;   // point to reflection coefficients
    IIR_Squelch_HPF.pvCoeffs  =  IIR_15k_hpf.pvCoeffs;   // point to ladder coefficients
    IIR_Squelch_HPF.numStages =  IIR_15k_hpf.numStages;      // number of stages
    IIR_Squelch_HPF.pState    =  iir_squelch_rx_state;                  // point to state array for IIR filter
    arm_fill_f32 ( 0.0, iir_squelch_rx_state, IIR_RX_STATE_ARRAY_SIZE );

    				//    ts.nr_long_tone_reset = true; // reset information about existing notches in filter passband



#ifdef USE_LMS_AUTONOTCH
			// AUTO NOTCH INIT START
			// LMS instance 2 - Automatic Notch Filter

			// Calculate "mu" (convergence rate) from user "Notch ConvRate" setting
    float32_t  mu_calc = log10f(((ts.dsp_notch_mu + 1.0)/1500.0) + 1.0);		// get user setting (0 = slowest)

    		// use "canned" init to initialize the filter coefficients
    arm_lms_norm_init_f32( &lmsData.lms2Norm_instance, ts.dsp_notch_numtaps, lmsData.lms2NormCoeff_f32, lmsData.lms2StateF32, mu_calc, IQ_BLOCK_SIZE ); // 32

    arm_fill_f32(0.0,lmsData.lms2_nr_delay,DSP_NOTCH_BUFLEN_MAX);

    if(reset_dsp_nr)             // are we to reset the coefficient buffer as well?
    {
        arm_fill_f32( 0.0, lmsData.lms2NormCoeff_f32, DSP_NOTCH_NUMTAPS_MAX);      // yes - zero coefficient buffers
    }

    if((ts.dsp_notch_delaybuf_len > DSP_NOTCH_BUFLEN_MAX) || (ts.dsp_notch_delaybuf_len < DSP_NOTCH_BUFLEN_MIN))
    {
        ts.dsp_nr_delaybuf_len = DSP_NOTCH_DELAYBUF_DEFAULT;
    }
    	// AUTO NOTCH INIT END
#endif


// NEW SPECTRAL NOISE REDUCTION
    // convert user setting of noise reduction to alpha NR parameter
    // alpha ranges from 0.9 to 0.999 [float32_t]
    // dsp_nr_strength is from 0 to 100 [uint8_t]
    //    ts.nr_alpha = 0.899 + ((float32_t)ts.dsp_nr_strength / 1000.0);
   //  ts.nr_alpha = 0.000 + (((float32_t)ts.dsp_nr_strength +00 ) / 200.1);

// NEW AUTONOTCH
    // not yet working !
    // set to passthrough
    //AudioNr_ActivateAutoNotch(0, 0);

    // Adjust decimation rate based on selected filter
    ads.decimation_rate = FilterPathInfo[ts.filter_path].sample_rate_dec;

//    ads.agc_decimation_scaling = ads.decimation_rate;
//    ads.agc_delay_buflen = AUDIO_DELAY_BUFSIZE/(ulong)ads.decimation_rate;	// calculate post-AGC delay based on post-decimation sampling rate

    // Set up ZOOM FFT FIR decimation filters
    // switch right FIR decimation filter depending on sd.magnify

    if ( sd.magnify > MAGNIFY_MAX )  { sd.magnify = MAGNIFY_MIN; }

    arm_fir_decimate_init_f32 ( &DECIMATE_ZOOM_FFT_I,	FirZoomFFTDecimate[sd.magnify].numTaps,
														( 1 << sd.magnify ),          // Decimation factor
														FirZoomFFTDecimate[sd.magnify].pCoeffs,
														decimZoomFFTIState,            // Filter state variables
														FIR_RXAUDIO_BLOCK_SIZE);      // 32

    arm_fir_decimate_init_f32 ( &DECIMATE_ZOOM_FFT_Q,	FirZoomFFTDecimate[sd.magnify].numTaps,
														(1 << sd.magnify),          // Decimation factor
														FirZoomFFTDecimate[sd.magnify].pCoeffs,
														decimZoomFFTQState,            // Filter state variables
														FIR_RXAUDIO_BLOCK_SIZE);      // 32

    // Set up RX decimation/filter
    // this filter instance is also used for Convolution !
#ifdef USE_CONVOLUTION
// TODO: insert decimation filter settings for convolution filter HERE
#else
    if (FilterPathInfo[ts.filter_path].dec != NULL)
    {
        const arm_fir_decimate_instance_f32* dec = FilterPathInfo[ts.filter_path].dec;

#if defined(STM32F4) && defined(USE_LMS_AUTONOTCH)
        // FIXME: Better solution (e.g. improve graphics performance, better data structures ... )
        // this code is a hack to reduce processor load for STM32F4 && SPI display
        // which causes UI lag
        // in this case we simply use a less power-eating filter (lower number of taps)
        // one problem is that we use the not so good filter
        // even if the autonotch / nr is not active and we could use the good filter

      /*  if (dec == &FirRxDecimate_sideband_supp && ts.display->use_spi == true)
        {
            dec = &FirRxDecimate;
        } */
#endif


		arm_fir_decimate_init_f32 ( &DECIMATE_RX_I,		dec->numTaps,            // Number of taps in FIR filter
														ads.decimation_rate,
														dec->pCoeffs,            // Filter coefficients
														decimState_I,            // Filter state variables
														FIR_RXAUDIO_BLOCK_SIZE ); // 32


        arm_fir_decimate_init_f32 ( &DECIMATE_RX_Q,		dec->numTaps,            // Number of taps in FIR filter
														ads.decimation_rate,
														dec->pCoeffs,            // Filter coefficients
														decimState_Q,            // Filter state variables
														FIR_RXAUDIO_BLOCK_SIZE ); // 32


        arm_fir_decimate_init_f32 ( &DECIMATE_TX,		dec->numTaps,              // Number of taps in FIR filter
														ads.decimation_rate,
														dec->pCoeffs,              // Filter coefficients
														decimState_Tx,             // Filter state variables
														FIR_RXAUDIO_BLOCK_SIZE ); // 32
    }
    else
    {
        DECIMATE_RX_I.numTaps = 0;
        DECIMATE_RX_I.pCoeffs = NULL;
        DECIMATE_RX_Q.numTaps = 0;
        DECIMATE_RX_Q.pCoeffs = NULL;
    }
#endif

    // this filter instance is also used for Convolution !
#ifdef USE_CONVOLUTION
// TODO: insert interpolation filter settings for convolution filter HERE
#else
    // Set up RX interpolation/filter
    // NOTE:  Phase Length MUST be an INTEGER and is the number of taps divided by the decimation rate, and it must be greater than 1.

        if (FilterPathInfo[ts.filter_path].interpolate != NULL)
        {
            arm_fir_interpolate_init_f32 ( &Rx_FIR_INTERPOLATE_12_to_48 ,	ads.decimation_rate,   //  4
																FilterPathInfo [ ts.filter_path ].interpolate->phaseLength,
																FilterPathInfo [ ts.filter_path ].interpolate->pCoeffs,
																Rx_interp_State_12_to_48,      // 8 + 16/4
																8  );  // 8
        }
        else
        {
        	Rx_FIR_INTERPOLATE_12_to_48.phaseLength = 0;
        	Rx_FIR_INTERPOLATE_12_to_48.pCoeffs     = NULL;
        }



    	// Set up TX interpolation/filter
        // NOTE:  Phase Length MUST be an INTEGER and is the number of taps divided by the decimation rate, and it must be greater than 1.
        for ( int chan = 0; chan < 2 ; chan++ )  // (int chan = 0; chan < NUM_AUDIO_CHANNELS; chan++)  le 2em servira au Tx apr�s Fit Hilbert
        {
            // remplace le filtre interpolation original RIF 16taps par un RIF 24taps
			// pour la proprete spectrale du Tx, applicable seulement si ( ts.tx_decimatedIQ) => ( (USB + LSB) & not_DIG_IQ )

			arm_fir_interpolate_init_f32 ( &Tx_FIR_INTERPOLATE_12_to_48 [ chan ],	4,  // en Tx decimation /4 seulement
																		        FirTxInterpolate.phaseLength, // 24 taps
																		        FirTxInterpolate.pCoeffs,
																		        Tx_interp_State_12_to_48 [ chan ],    // 8 + 24/4
																		        8  ); // 8 en Tx c'est toujours  8

        }

#endif

#ifdef USE_CONVOLUTION
    // Convolution Filter
    // calculate coeffs
    // for first trial, use hard-coded USB filter from 250Hz to 2700Hz
    // hardcoded sample rate and Blackman-Harris 4th term
    cbs.size     = 128; // 128 samples processed at one time, this defines the latency:
    // latency   = cbs.size / sample rate = 128/48000 = 2.7ms
    cbs.nc       = 1024; // use 1024 coefficients
    cbs.nfor     = cbs.nc / cbs.size; // number of blocks used for the uniformly partitioned convolution
    cbs.buffidx  = 0; // needs to be reset to zero each time new coeffs are being calculated
    AudioDriver_CalcConvolutionFilterCoeffs ( cbs.nc, 250.0, 2700.0, IQ_SAMPLE_RATE_F,  0,  1,  1.0 );
#endif

    arm_fir_decimate_init_f32 ( &DECIMATE_NR         , 4, 2, FIR_NR_decimate_coeffs,  FIR_decim_NR_State, 8 );
    arm_fir_init_f32          ( &Fir_NR_PRES_FILTRAGE, 4,    FIR_NR_decimate_coeffs , FIR_decim_NR_State, 8 );  // load "I" with "I" coefficients





    // should be a very light lowpass @2k7

   //  arm_fir_interpolate_init_f32( &INTERPOLATE_NR, 2, NR_INTERPOLATE_NO_TAPS, NR_interpolate_coeffs, interplNRState, FIR_RXAUDIO_BLOCK_SIZE);
    // should be a very light lowpass @2k7

    // Initialisation du filtre Interpolation 6Khz-->12Khz du Noise Reduction  (remplace l'ancien  filtre FIR 40 taps )
    //if (!ts.show_debug_info)
    {
		arm_iir_lattice_init_f32 ( &IIR_NR_Interpolation,   IIR_Noise_Reduction_Interpolation_2k86_LPF.numStages,       // number of stages
															IIR_Noise_Reduction_Interpolation_2k86_LPF.pkCoeffs,        // point to reflection coefficients
															IIR_Noise_Reduction_Interpolation_2k86_LPF.pvCoeffs,        // point to ladder coefficients
															IIR_Noise_Reduction_Interpol_PState,
															8 );  // Blocksize = 8 / Fs= 12Khz
    }
 /*   else
    {
    		arm_iir_lattice_init_f32 ( &IIR_NR_Interpolation,   IIR_Noise_Reduction_Interpolation_2k86_LPF_2.numStages,       // number of stages
    															IIR_Noise_Reduction_Interpolation_2k86_LPF_2.pkCoeffs,        // point to reflection coefficients
    															IIR_Noise_Reduction_Interpolation_2k86_LPF_2.pvCoeffs,        // point to ladder coefficients
    															IIR_Noise_Reduction_Interpol_PState,
    															8 );  // Blocksize = 8 / Fs= 12Khz
    }*/




    ads.dsp_zero_count = 0;		// initialize "zero" count to detect if DSP has crashed

    //  if (dmod_mode == DEMOD_AM || dmod_mode == DEMOD_SAM)   // NIZZZ demasqu� pour reduire la tache et peut etre aussi la m�moire
	  {
		  AudioDriver_SetRxAudioProcessingSAM ( dmod_mode );
	  }

    AudioFilter_Init_Rx_Hilbert_FIR ( dmod_mode ); // this switches the Hilbert/FIR-filters

    AudioDriver_SetupAgcWdsp();

  //  non_os_delay();
    if (ts.audio_dac_output_muting_buffer_count < 50)  ts.audio_dac_output_muting_buffer_count += 50;

     // Unlock - re-enable filtering
    if  ( ts.dsp_inhibit  ) {  ts.dsp_inhibit--;  }
    if  ( ads.af_disabled ) {  ads.af_disabled--; }


}

//********************************************************************************************************************************
//******************  Initialisation du filtre Rx Passe Haut 75 Hz pour audio avant AGCW  *******************************************
//********************************************************************************************************************************

void   AudioDriver_Rx_HP_Filter_Init()
{
	ads.af_disabled++;
	ts.dsp_inhibit++;										// Init Rx  HPF audio filter


	 const   arm_iir_lattice_instance_f32*    IIR_FilterSelected_ptr;
	 IIR_Rx_HP_Filter.numStages   = 3;


	switch ( ts.rx_hp_filter )
	{
		case 0: 	IIR_FilterSelected_ptr = NULL;	          IIR_Rx_HP_Filter.numStages = 0;   break;
		case 1: 	IIR_FilterSelected_ptr = &IIR_50HZ_HPF;	                                    break; // NIZZZ
		case 2: 	IIR_FilterSelected_ptr = &IIR_65HZ_HPF;	                                    break; // NIZZZ
		case 3:     IIR_FilterSelected_ptr = &IIR_75HZ_HPF; 							        break;
		case 4:     IIR_FilterSelected_ptr = &IIR_100HZ_HPF;		                            break;
		case 5:     IIR_FilterSelected_ptr = &IIR_150HZ_HPF;		                            break;
		case 6:     IIR_FilterSelected_ptr = &IIR_200HZ_HPF;		                            break;
	}


    if ( IIR_Rx_HP_Filter.numStages > 0)
	{
		arm_iir_lattice_init_f32 ( &IIR_Rx_HP_Filter,	IIR_Rx_HP_Filter.numStages,                        // number of stages
													    IIR_FilterSelected_ptr->pkCoeffs,               // point to reflection coefficients
													    IIR_FilterSelected_ptr->pvCoeffs,               // point to ladder coefficients
													    IIR_Rx_HP_Filter.pState = iir_Rx_HPF_State,
													    8 );  // 32/4 = 8 �chantillons apr�s d�cimation
	}


    IIR_FilterSelected_ptr = &IIR_FM_2600HZ_LPF;
	arm_iir_lattice_init_f32 ( &IIR_Rx_LP_Filter_2K6, 	    IIR_FilterSelected_ptr->numStages,          // number of stages
														    IIR_FilterSelected_ptr->pkCoeffs,          // point to reflection coefficients
														    IIR_FilterSelected_ptr->pvCoeffs,          // point to ladder coefficients
															IIR_Rx_LP_Filter_2K6.pState = iir_Rx_LPF_2K6_FM,
														    32 );  // 32/4 = 8 �chantillons apr�s d�cimation

	IIR_FilterSelected_ptr = &IIR_FM_22HZ_HPF;
	arm_iir_lattice_init_f32 ( &IIR_Rx_HP_Filter_22Hz, 	    IIR_FilterSelected_ptr->numStages,          // number of stages
															IIR_FilterSelected_ptr->pkCoeffs,          // point to reflection coefficients
															IIR_FilterSelected_ptr->pvCoeffs,          // point to ladder coefficients
															IIR_Rx_HP_Filter_22Hz.pState = IIR_Rx_HP_Filter_22Hz_state,
															32 );  // 32/4 = 8 echantillons apres decimation

     IIR_FilterSelected_ptr = &IIR_16k8_LPF;    // &IIR_16k1_LPF;  &IIR_16k5_LPF;

    arm_iir_lattice_init_f32 ( &IIR_Rx_LPF_16K_Filter_I,	IIR_FilterSelected_ptr->numStages,                           // number of stages
    													    IIR_FilterSelected_ptr->pkCoeffs,               // point to reflection coefficients
    													    IIR_FilterSelected_ptr->pvCoeffs,               // point to ladder coefficients
															IIR_Rx_LPF_16K_Filter_I.pState = iir_Rx_LPF_16K_I_State,
    													    64);  //bloc de 64 points @ 96Khz


    IIR_FilterSelected_ptr = &IIR_16k8_LPF;     // &IIR_16k1_LPF;  &IIR_16k5_LPF;

    arm_iir_lattice_init_f32 ( &IIR_Rx_LPF_16K_Filter_Q,	IIR_FilterSelected_ptr->numStages,                           // number of stages
    													    IIR_FilterSelected_ptr->pkCoeffs,               // point to reflection coefficients
    													    IIR_FilterSelected_ptr->pvCoeffs,               // point to ladder coefficients
															IIR_Rx_LPF_16K_Filter_Q.pState = iir_Rx_LPF_16K_Q_State,
    													    64);  //bloc de 64 points @ 96Khz


    // Filtre  LPF IIR 2em ordre pour interpolation Rx avant Haut-Parleur (éviter de saturer les ampli BF par les résidus de la 48Khz)

/*    IIR_FilterSelected_ptr = &IIR_11k5_LPF;

	arm_iir_lattice_init_f32 ( &IIR_Rx_Interpo_48_to_96,   IIR_FilterSelected_ptr->numStages,         // number of stages
														   IIR_FilterSelected_ptr->pkCoeffs,          // point to reflection coefficients
														   IIR_FilterSelected_ptr->pvCoeffs,          // point to ladder coefficients
														   IIR_Rx_Interpo_48_to_96.pState = iir_Rx_Interpo_48_to_96_State,
														   64  );  //bloc de 64 points @ 96Khz
*/

    /*
    IIR_FilterSelected_ptr = &IIR_5k7_LPF; // Filtre LPF microphone avant decimation 96Khz-->48Khz
 	IIR_Tx_LPF_5K7_Filter.numStages   = 4;
	arm_iir_lattice_init_f32 ( &IIR_Tx_LPF_5K7_Filter,	IIR_Tx_LPF_5K7_Filter.numStages,                           // number of stages
														IIR_FilterSelected_ptr->pkCoeffs,               // point to reflection coefficients
														IIR_FilterSelected_ptr->pvCoeffs,               // point to ladder coefficients
														IIR_Tx_LPF_5K7_Filter.pState = iir_Tx_LPF_5K7_state,
														64);  //bloc de 64 points @ 96Khz
	*/


	 // IIR_FilterSelected_ptr = &IIR_3k84_LPF;          // Filtre LPF microphone avant decimation 96Khz-->48Khz
    	IIR_FilterSelected_ptr = &IIR_Decim_4k_LPF_96k;  // Filtre LPF microphone avant decimation 96Khz-->48Khz

		arm_iir_lattice_init_f32 ( &IIR_Tx_LPF_3K84_Filter,	IIR_FilterSelected_ptr->numStages,                           // number of stages
															IIR_FilterSelected_ptr->pkCoeffs,               // point to reflection coefficients
															IIR_FilterSelected_ptr->pvCoeffs,               // point to ladder coefficients
															IIR_Tx_LPF_3K84_Filter.pState = iir_Tx_LPF_3K84_State,
															64 );  //bloc de 64 points @ 96Khz


    // Unlock - re-enable filtering
	if  ( ts.dsp_inhibit  ) {  ts.dsp_inhibit--;  }
    if  ( ads.af_disabled ) {  ads.af_disabled--; }
}
//********************************************************************************************************************************
/*
void AudioDriver_Voice_Tx_Filter_RAZ ( )
{
	for (uint i = 0; i<32 ; i++) { adb.i_buffer[i] = 0; }
	arm_fir_decimate_f32_Nizar ( &DECIMATE_RX_I, adb.i_buffer, adb.i_buffer, 32 );

	for (uint i = 0; i<32 ; i++) { adb.i_buffer[i] = 0; }
	arm_fir_decimate_f32_Nizar ( &DECIMATE_RX_Q, adb.i_buffer, adb.i_buffer, 32 );

	for (uint i = 0; i<32 ; i++) { adb.i_buffer[i] = 0; }
	// arm_fir_interpolate_f32_Nizar ( &INTERPOLATE_RX, adb.i_buffer, adb.i_buffer, 16 );




	// memset ( decimState_I           , 0, (32+43)*4 );
    // memset ( decimState_Q           , 0, (32+43)*4 );
	// memset ( Rx_interp_State[0]         , 0,    48*2*4 );
	// memset ( Fir_Tx_Hilbert_State_I , 0,     233*4 );
	// memset ( Fir_Tx_Hilbert_State_Q , 0,     233*4 );
}
*/
//********************************************************************************************************************************

void  AudioDriver_Voice_Tx_Filter_Init ( uint8_t dmod_mode )
{
    // Init TX audio filter - Do so "manually" since built-in init functions don't work with CONST coefficients
	ads.tx_filter_adjusting++;

    const  arm_iir_lattice_instance_f32*  IIR_TXFilterSelected_ptr;

    IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_75HZ; // filtre par defaut si FS= 48Khz

    if (ts.tx_decimatedIQ)   // si Fs=12Khz
    {
		switch ( ts.tx_hpf_filter )					// ts.tx_decimatedIQ = ( (USB + LSB) & not_DIG_IQ )
		{
			case 0:  IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_65HZ_DECIM;   break;
			case 1:  IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_75HZ_DECIM;   break;
			case 2:  IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_80HZ_DECIM;   break;
			case 3:  IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_90HZ_DECIM;   break;
			case 4:  IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_100HZ_DECIM;  break;
			case 5:  IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_120HZ_DECIM;  break;
			case 6:  IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_140HZ_DECIM;  break;
			case 7:  IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_160HZ_DECIM;  break;
			default: IIR_TXFilterSelected_ptr   =  &IIR_TX_HPF_65HZ_DECIM;   break;
		}
    }

        uint16_t    block_size_tmp = ts.tx_decimatedIQ ?  8 : 32;	// ts.tx_decimatedIQ = ( (USB + LSB) & not_DIG_IQ )


        										// Initialisation Tx  HPF 75 Hz  for voice audio
            arm_iir_lattice_init_f32 ( &IIR_HPF_TXFilter,	IIR_TXFilterSelected_ptr->numStages,       // number of stages
            												IIR_TXFilterSelected_ptr->pkCoeffs,        // point to reflection coefficients
    														IIR_TXFilterSelected_ptr->pvCoeffs,        // point to ladder coefficients
    														IIR_HPF_TXFilter.pState = iir_tx_hpf_state,
    														block_size_tmp );


/*
            arm_iir_lattice_init_f32 ( &IIR_TX_Voice_Notch_1200HZ,                                    2,
            														 IIR_TX_Voice_Notch_1200HZ.pkCoeffs,
																	 IIR_TX_Voice_Notch_1200HZ.pvCoeffs,
																	 IIR_TX_Voice_Notch_state ,
																	 8 );
*/

               //**********************************************************************************************
               //******* (Tx seulement )  Filtre IIR antirepliement pour interpolation 48Khz --> 96Khz ********
               //**********************************************************************************************

            IIR_TXFilterSelected_ptr   = &IIR_16k5_LPF;

            arm_iir_lattice_init_f32 ( &IIR_AntiAlias_48_to_96_I,	IIR_TXFilterSelected_ptr->numStages,  // number of stages
																	IIR_TXFilterSelected_ptr->pkCoeffs,   // point to reflection coefficients
																	IIR_TXFilterSelected_ptr->pvCoeffs,   // point to ladder coefficients
																	IIR_AntiAlias_48_to_96_I.pState = iir_tx_I_48_to_96_State,
																	64 );

            IIR_TXFilterSelected_ptr   = &IIR_16k5_LPF;
			arm_iir_lattice_init_f32 ( &IIR_AntiAlias_48_to_96_Q,	IIR_TXFilterSelected_ptr->numStages,  // number of stages
																	IIR_TXFilterSelected_ptr->pkCoeffs,   // point to reflection coefficients
																	IIR_TXFilterSelected_ptr->pvCoeffs,   // point to ladder coefficients
																	IIR_AntiAlias_48_to_96_Q.pState = iir_tx_Q_48_to_96_State,
																	64 );

               //****************************************************************************************************
               //****************************************************************************************************

    if( dmod_mode != DEMOD_FM )	 						// not FM - use bandpass filter that restricts low and, stops at 2.7 kHz
    {
        switch ( ts.tx_filter )                      // ts.tx_decimatedIQ = ( (USB + LSB) & not_DIG_IQ )
        {
			case TX_FILTER_BASS: 	IIR_TXFilterSelected_ptr = ts.tx_decimatedIQ ? &IIR_TX_WIDE_WIDE_DECIM : &IIR_TX_WIDE_BASS;   break;
			case TX_FILTER_WIDE: 	IIR_TXFilterSelected_ptr = ts.tx_decimatedIQ ? &IIR_TX_WIDE_WIDE_DECIM : &IIR_TX_WIDE_WIDE;	  break; // NIZZZ
			case TX_FILTER_TENOR:   IIR_TXFilterSelected_ptr = ts.tx_decimatedIQ ? &IIR_TX_WIDE_WIDE_DECIM : &IIR_TX_WIDE_TREBLE; break;
			default:				IIR_TXFilterSelected_ptr = ts.tx_decimatedIQ ? &IIR_TX_WIDE_WIDE_DECIM : &IIR_TX_SOPRANO;     break;
        }
    }
    else	 	// This is FM - use a filter with "better" lows and highs more appropriate for FM
    {
        IIR_TXFilterSelected_ptr = &IIR_TX_2k7_FM;
    }

    arm_iir_lattice_init_f32 ( &IIR_TX_LPF_Filter,	IIR_TXFilterSelected_ptr->numStages,  // number of stages
												    IIR_TXFilterSelected_ptr->pkCoeffs,   // point to reflection coefficients
												    IIR_TXFilterSelected_ptr->pvCoeffs,   // point to ladder coefficients
												    IIR_TX_LPF_Filter.pState = iir_tx_State,
												    block_size_tmp );

    // Filtre passe tout 137Hz du microphone pour retarder les basses

    IIR_TXFilterSelected_ptr = &IIR_TX_PASS_TOUT_137HZ;

	arm_iir_lattice_init_f32 ( &IIR_TX_PASS_TOUT_Filter, IIR_TXFilterSelected_ptr->numStages,  // number of stages
													     IIR_TXFilterSelected_ptr->pkCoeffs,   // point to reflection coefficients
													     IIR_TXFilterSelected_ptr->pvCoeffs,   // point to ladder coefficients
														 IIR_TX_PASS_TOUT_Filter.pState = iir_tx_pass_tout,
													     8 );



	IIR_TXFilterSelected_ptr = &IIR_TX_PASS_TOUT_182HZ;

	arm_iir_lattice_init_f32 ( &IIR_TX_PASS_TOUT_Filter_2, IIR_TXFilterSelected_ptr->numStages,  // number of stages
														   IIR_TXFilterSelected_ptr->pkCoeffs,   // point to reflection coefficients
														   IIR_TXFilterSelected_ptr->pvCoeffs,   // point to ladder coefficients
														   IIR_TX_PASS_TOUT_Filter_2.pState = iir_tx_pass_tout_2,
														   8 );

/*	IIR_TXFilterSelected_ptr = &IIR_RX_PASS_TOUT_278HZ;

	arm_iir_lattice_init_f32 ( &IIR_RX_PASS_TOUT_Filter,   IIR_TXFilterSelected_ptr->numStages,  // number of stages
														   IIR_TXFilterSelected_ptr->pkCoeffs,   // point to reflection coefficients
														   IIR_TXFilterSelected_ptr->pvCoeffs,   // point to ladder coefficients
														   IIR_RX_PASS_TOUT_Filter.pState = iir_Rx_pass_tout,
														   8 );
*/

    						// Tx  LPF 52 Hz pour adoucire la commande du compresseur de la Voix Tx



	arm_iir_lattice_init_f32 ( &IIR_LPF_Compressor,   	   IIR_LPF_52Hz_Compressor.numStages,  // number of stages
														   IIR_LPF_52Hz_Compressor.pkCoeffs,   // point to reflection coefficients
														   IIR_LPF_52Hz_Compressor.pvCoeffs,   // point to ladder coefficients
														   IIR_LPF_Compressor.pState = iir_tx_lpf_compressor_State,
													       block_size_tmp );


    if ( ads.tx_filter_adjusting )  { ads.tx_filter_adjusting--; }
}

			//*----------------------------------------------------------------------------
			//* Function Name       : Audio_Init
			//* Object              :
			//* Object              : init filters
			//* Input Parameters    :
			//* Output Parameters   :
			//* Functions called    :
			//*----------------------------------------------------------------------------

static void AudioDriver_InitFilters(void)
{
    AudioDriver_SetRxAudioProcessing ( ts.dmod_mode, false );

    AudioDriver_Voice_Tx_Filter_Init ( ts.dmod_mode ); // Tx filter HP + LP  filters


// N96K B4: initialize the FreeDV filter instances in the opt-in FreeDV build as well.
#if !defined(IS_SMALL_BUILD) || defined(USE_FREEDV)      // pour reduire la memoire RAM avec version Small
    IIR_biquad_FreeDV_I.pCoeffs    = FreeDV_coeffs[0];  // FreeDV Filter test -DL2FW-
    IIR_biquad_FreeDV_Q.pCoeffs    = FreeDV_coeffs[0];

    								// temporary installed audio filter's coefficients
    IIR_FreeDV_RX_Filter.numStages = IIR_TX_WIDE_TREBLE.numStages; // using the same for FreeDV RX Audio as for TX
    IIR_FreeDV_RX_Filter.pkCoeffs  = IIR_TX_WIDE_TREBLE.pkCoeffs;   // but keeping it constant at "Tenor" to avoid
    IIR_FreeDV_RX_Filter.pvCoeffs  = IIR_TX_WIDE_TREBLE.pvCoeffs;   // influence of TX setting in RX path
    IIR_FreeDV_RX_Filter.pState    = iir_tx_State;
    arm_fill_f32 ( 0.0,  iir_FreeDV_RX_State, IIR_RX_STATE_ARRAY_SIZE );
#endif

}

// #if USE_OBSOLETE_NOISEBLANKER
//*----------------------------------------------------------------------------
//* Function Name       : audio_rx_noise_blanker [KA7OEI]
//* Object              : noise blanker
//* Object              :
//* Input Parameters    : I/Q 16 bit audio data, size of buffer
//* Output Parameters   :
//* Functions called    :
//*----------------------------------------------------------------------------
// La nouvelle fonction de NB que j'ai modifi� pour fonctionner au plus large spectre possible

/*

static  void  AudioDriver_NoiseBlanker ( AudioSample_t * const src, int16_t blockSize )
{
    static  int32_t	 delay_buf [ 16 ];    // NIZ32  int16_t
    static  uint8_t	 delbuf_inptr = 0,  delbuf_outptr = 2;
    ulong	         i;
    float32_t        sig;
    float32_t        nb_short_setting;
 //	static  float    avg_sig;
    static	uchar	 nb_delay = 0;
    static  float	 nb_agc   = 0;


    //if (   	   //            (ts.nb_setting > 0) &&
				   //			 ( ts.dsp_active & DSP_NB_ENABLE)  &&  (ts.dmod_mode != DEMOD_FM) )
                   //        &&  ( ts.dmod_mode != DEMOD_AM && ts.dmod_mode != DEMOD_FM)
                   //        &&  ( FilterPathInfo[ts.filter_path].sample_rate_dec != RX_DECIMATION_RATE_24KHZ ))
        		   //   bail out if noise blanker disabled, in AM/FM mode, or set to 10 kHz

    if ( ( ts.dsp_active & DSP_NB_ENABLE)  &&  (ts.dmod_mode != DEMOD_FM) )
    {
        // nb_short_setting   =  ts.nb_setting;		// convert and rescale NB1 setting for higher resolution in adjustment
        // nb_short_setting   = ( 15 - ts.nb_setting) * 0.5  + 3 ; // * nb_agc;
    	   nb_short_setting   = ts.nb_setting_float * nb_agc;

        for ( i = 0; i < blockSize;  i++ )	 		// Noise blanker function
        {
        	delay_buf [ delbuf_inptr++ ] =  src [ i ].l;	            // copy first byte into delay buffer
        	delay_buf [ delbuf_inptr++ ] =  src [ i ].r;	            // copy second byte into delay buffer

            sig                          =  abs ( I2S_correctHalfWord ( src [ i ].l ) );	// fabs	// get signal amplitude.  We need only look at one of the two audio channels since they will be the same.

         // sig                         /=  ads.codec_gain_calc;	// NIZZZ   Scale for codec A/D gain adjustment
         // avg_sig                      = (avg_sig * NB_AVG_WEIGHT) + ((float)(*src) * NB_SIG_WEIGHT);	// IIR-filtered short-term average signal level (e.g. low-pass audio)


            nb_agc  +=  ( sig -  nb_agc ) * 0.005;	      // 0.01	//ads.nb_sig_filt    IIR-filtered "AGC" of current overall signal level

            if  ( sig  > nb_short_setting ) // && (nb_delay == 0) )	 	// did a pulse exceed the threshold?
            {
                nb_delay = 18;   //18;	               // -7 pts avant /+11  pts après  a Razer Zero	// yes - set the blanking duration counter
            }

            if(!nb_delay)	 		// blank counter not active
            {
                src [ i ].l  =  delay_buf [ delbuf_outptr++ ];		// pass through delayed audio, unchanged
                src [ i ].r  =  delay_buf [ delbuf_outptr++ ];
            }
            else	 	// It is within the blanking pulse period
            {
                src [ i ].l = 0;        // (int16_t) avg_sig;		// set the audio buffer to "mute" during the blanking period
                src [ i ].r = 0;        // (int16_t) avg_sig;
                nb_delay--;				// count down the number of samples that we are to blank
                delbuf_outptr += 2u;
            }
            		                     // RINGBUFFER
		   if ( delbuf_outptr  == 16u ) delbuf_outptr = 0;
		   if ( delbuf_inptr   == 16u ) delbuf_inptr  = 0;
        }
    }
}
*/

//********************************************************************************************************

static  void  AudioDriver_NoiseBlanker_int ( AudioSample_t * const src)
{
    static  int32_t	  delay_buf [ 32 ];    // 32  // NIZ32  int16_t
    static  uint8_t	  delbuf_inptr = 0u,   delbuf_outptr = 10u; // Pour assurer un retard de 7 echantillons
    uint32_t          i;
    int32_t           abs_sig;
    uint32_t          NB_Threshold;
    int32_t          coeff_div  = 300;

    static	uchar	  nb_delay   = 0u;

    static  int32_t	     nb_agc     =  0;
            int16_t     *val_16     =  (int16_t*) src;
            uint32_t     BS_64      = 64;

    // if  ( ts.dmod_mode != DEMOD_FM )
    {
        NB_Threshold   =  ( ( nb_agc * ts.nb_setting_int_coeff ) >> 1u) ;


        for ( i = 0; i < BS_64;  i++ )	 		                                  // Noise blanker function
        {
        	delay_buf [ delbuf_inptr     ] =  src [ i ].l;	                      // copy first byte into delay buffer
        	abs_sig                        =  ((uint32_t) abs ( val_16[ i << 2u ] ) << 5u) ;  // * 32 pour améliorer la resolution binaire avant division entiere par 200
        	delay_buf [ delbuf_inptr + 1u] =  src [ i ].r;	                      // copy second byte into delay buffer

        	nb_agc  += ( abs_sig -  nb_agc ) / coeff_div ;   // 300 // moyennage des valeurs absolues sur 300 points ( 3.1 ms )

            if  ( abs_sig  > NB_Threshold )     // did a pulse exceed the threshold?
            {
                nb_delay = 27u;   // 27 //18;	            // -7 pts avant /+11  pts après  a Razer Zero	// yes - set the blanking duration counter
            }

            if ( !nb_delay )	 		  // blank counter not active
            {
                src [ i ].l  =  delay_buf [ delbuf_outptr     ];		// pass through delayed audio, unchanged
                src [ i ].r  =  delay_buf [ delbuf_outptr + 1u];
            }
            else	 	// It is within the blanking pulse period
            {
                src [ i ].l = 0;        // set the audio buffer to "mute" during the blanking period
                src [ i ].r = 0;
                nb_delay--;				// count down the number of samples that we are to blank
            }

            delbuf_outptr  += 2u;
            delbuf_inptr   += 2u;

            delbuf_outptr  &= 0x1F;
            delbuf_inptr   &= 0x1F;
        }
    }
}


//
//*----------------------------------------------------------------------------
//* Function Name       : audio_rx_freq_conv [KA7OEI]
//* Object              : Does I/Q frequency conversion
//* Object              :
//* Input Parameters    : size of array on which to work; dir: determines direction of shift - see below;  Also uses variables in ads structure
//* Output Parameters   : uses variables in ads structure
//* Functions called    :
//*----------------------------------------------------------------------------

void   AudioDriver_FreqConversion ( float32_t* i_buffer, float32_t* q_buffer, uint16_t const  blockSize, int16_t dir )
{
    static  bool  recalculate_Osc  =  true;  //  false;
    // keeps the generated data for frequency conversion
    static  float32_t                   Osc_I_buffer [ IQ_BLOCK_SIZE ];
    static  float32_t                   Osc_Q_buffer [ IQ_BLOCK_SIZE ];

    // assert  ( blockSize <= IQ_BLOCK_SIZE );


    //
    // Below is the "on-the-fly" version of the frequency translator, generating a "live" version of the oscillator (NCO), which can be any
    // frequency, based on the values of "ads.Osc_Cos" and "ads.Osc_Sin".  While this does function, the generation of the SINE takes a LOT
    // of processor time!
    //
    // The values for the NCO are configured in the function "audio_driver_config_nco()".
    //
    // This commented-out version also lacks the "dir" (direction) control which selects either high or low side translation.
    // This code is left here so that everyone can see how it is actually done in plain, "un-ARM" code.
    //


    if ( recalculate_Osc )	 		// have we already calculated the sine wave?
    {
    	float32_t     p =  0.70710678;  // sin( 45 degree)
    	float32_t     n = -0.70710678;  // sin(-45 degree)

    	for ( int i = 0; i < blockSize; i += 8u )
    	{
			Osc_I_buffer[ i      ] =  0;    Osc_Q_buffer[ i      ] =  1;
			Osc_I_buffer[ i + 1u ] =  p;    Osc_Q_buffer[ i + 1u ] =  p;
			Osc_I_buffer[ i + 2u ] =  1;    Osc_Q_buffer[ i + 2u ] =  0;
			Osc_I_buffer[ i + 3u ] =  p;    Osc_Q_buffer[ i + 3u ] =  n;
			Osc_I_buffer[ i + 4u ] =  0;    Osc_Q_buffer[ i + 4u ] = -1;
			Osc_I_buffer[ i + 5u ] =  n;    Osc_Q_buffer[ i + 5u ] =  n;
			Osc_I_buffer[ i + 6u ] = -1;    Osc_Q_buffer[ i + 6u ] =  0;
			Osc_I_buffer[ i + 7u ] =  n;    Osc_Q_buffer[ i + 7u ] =  p;
    	}
        recalculate_Osc = false;	// signal that once we have generated the quadrature sine waves, we shall not do it again
     }


    if ( ts.iq_freq_mode == FREQ_IQ_CONV_P12KHZ || ts.iq_freq_mode == FREQ_IQ_CONV_M12KHZ )
    {
        //**********************************************************************************
        // *  Frequency translation by Fs/4 without multiplication
        // *  Lyons (2011): chapter 13.1.2 page 646
        // *  this is supposed to be much more efficient than a standard quadrature oscillator
        // *  with precalculated sin waves
        // *  Thanks, Clint, for pointing my interest to this method!, DD4WH 2016_12_28
       //  **********************************************************************************
        if ( dir )
        {
            // this is for +Fs/4 [moves receive frequency to the left in the spectrum display]
            for(int i = 0; i < blockSize; i += 4u)
            {// xnew(0)         =  xreal(0) + jximag(0)
             // leave as it is!
             // xnew(1)         = - ximag(1) + jxreal(1)
                float32_t hh1   = - q_buffer[i + 1u];
                float32_t hh2   =   i_buffer[i + 1u];
                i_buffer[i + 1u] =   hh1;
                q_buffer[i + 1u] =   hh2;
             // xnew(2)         = - xreal(2) - jximag(2)
                hh1             = - i_buffer[i + 2u];
                hh2             = - q_buffer[i + 2u];
                i_buffer[i + 2u] =   hh1;
                q_buffer[i + 2u] =   hh2;
             // xnew(3)         = + ximag(3) - jxreal(3)
                hh1             =   q_buffer[i + 3u];
                hh2             = - i_buffer[i + 3u];
                i_buffer[i + 3u] =   hh1;
                q_buffer[i + 3u] =   hh2;
            }
        }

        else // dir == 0
        {
            // this is for -Fs/4 [moves receive frequency to the right in the spectrum display]
            for(int i = 0; i < blockSize; i += 4u)
            { // xnew(0)        =  xreal(0) + jximag(0)
              // leave as it is!
              // xnew(1)        = ximag(1) - jxreal(1)
                float32_t hh1   =   q_buffer[i + 1u];
                float32_t hh2   = - i_buffer[i + 1u];
                i_buffer[i + 1u] = hh1;
                q_buffer[i + 1u] = hh2;
             // xnew(2)         = -xreal(2) - jximag(2)
                hh1             = - i_buffer[i + 2u];
                hh2             = - q_buffer[i + 2u];
                i_buffer[i + 2u] = hh1;
                q_buffer[i + 2u] = hh2;
             // xnew(3)         = -ximag(3) + jxreal(3)
                hh1             = - q_buffer[i + 3u];
                hh2             =   i_buffer[i + 3u];
                i_buffer[i + 3u] = hh1;
                q_buffer[i + 3u] = hh2;
            }
        }
    }
    else  // frequency translation +6kHz or -6kHz
    {
        float32_t  c_buffer[ blockSize ];
        float32_t  d_buffer[ blockSize ];
        float32_t  e_buffer[ blockSize ];
        float32_t  f_buffer[ blockSize ];

        // Do frequency conversion using optimized ARM math functions [KA7OEI]
        arm_mult_f32 ( q_buffer, Osc_Q_buffer, c_buffer, blockSize ); // multiply products for converted I channel
        arm_mult_f32 ( i_buffer, Osc_I_buffer, d_buffer, blockSize );
        arm_mult_f32 ( q_buffer, Osc_I_buffer, e_buffer, blockSize );
        arm_mult_f32 ( i_buffer, Osc_Q_buffer, f_buffer, blockSize );    // multiply products for converted Q channel

        if(!dir)	 	// Conversion is "above" on RX (LO needs to be set lower)
        {
            arm_add_f32 ( f_buffer, e_buffer, i_buffer, blockSize );	// summation for I channel
            arm_sub_f32 ( c_buffer, d_buffer, q_buffer, blockSize );	// difference for Q channel
        }
        else	 	// Conversion is "below" on RX (LO needs to be set higher)
        {
            arm_add_f32 ( c_buffer, d_buffer, q_buffer, blockSize );	// summation for I channel
            arm_sub_f32 ( f_buffer, e_buffer, i_buffer, blockSize );	// difference for Q channel
        }
    }
}



///***********************************************************************************************************

//*************************************************************************************************************

///***********************************************************************************************************



#ifdef USE_FREEDV
static bool AudioDriver_RxProcessorFreeDV (AudioSample_t * const src, float32_t * const dst, int16_t blockSize)
{
    // Freedv Test DL2FW
    static  int16_t outbuff_count             = -3;  //set to -3 since we simulate that we start with history
    static  int16_t trans_count_in            = 0;
    static  int16_t FDV_TX_fill_in_pt         = 0;
    static  FDV_Audio_Buffer* out_buffer      = NULL;
    static  int16_t modulus_NF = 0, mod_count = 0;
    bool lsb_active = (ts.dmod_mode == DEMOD_LSB || (ts.dmod_mode == DEMOD_DIGI && ts.digi_lsb == true));


    static float32_t History[3] = {0.0,0.0,0.0};

    // If source is digital usb in, pull from USB buffer, discard line or mic audio and
    // let the normal processing happen


    if ( ts.digital_mode == DigitalMode_FreeDV )
    { //we are in freedv-mode


        // *****************************   DV Modulator goes here - ads.a_buffer must be at 8 ksps

        // Freedv Test DL2FW

        // we have to add a decimation filter here BEFORE we decimate
        // for decimation-by-6 the stopband frequency is 48/6*2 = 4kHz
        // but our audio is at most 3kHz wide, so we should use 3k or 2k9


        // this is the correct DECIMATION FILTER (before the downsampling takes place):
        // use it ALWAYS, also with TUNE tone!!!


        if ( ts.filter_path != 65 )
        {   // just for testing, when Filter #65 (10kHz LPF) is selected, antialiasing is switched off
            arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_FreeDV_I, adb.i_buffer, adb.i_buffer, blockSize );
            arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_FreeDV_Q, adb.q_buffer, adb.q_buffer, blockSize );
        }

        // DOWNSAMPLING
        for ( int k = 0; k < blockSize; k++ )
        {
            if (k % 6 == modulus_NF)  //every 6th sample has to be catched -> downsampling by 6
            {

                if (lsb_active == true)
                {
                    mmb.fdv_iq_buff[FDV_TX_fill_in_pt].samples[trans_count_in].real = ((int32_t)adb.q_buffer[k]);
                    mmb.fdv_iq_buff[FDV_TX_fill_in_pt].samples[trans_count_in].imag = ((int32_t)adb.i_buffer[k]);
                }
                else
                {
                    mmb.fdv_iq_buff[FDV_TX_fill_in_pt].samples[trans_count_in].imag = ((int32_t)adb.q_buffer[k]);
                    mmb.fdv_iq_buff[FDV_TX_fill_in_pt].samples[trans_count_in].real = ((int32_t)adb.i_buffer[k]);
                }

                trans_count_in++;
            }
        }

        modulus_NF += 4; //  shift modulus to not loose any data while overlapping
        modulus_NF %= 6;//  reset modulus to 0 at modulus = 12

        if (trans_count_in == FDV_BUFFER_SIZE) //yes, we really hit exactly 320 - don't worry
        {
            //we have enough samples ready to start the FreeDV encoding

            fdv_iq_buffer_add ( &mmb.fdv_iq_buff [ FDV_TX_fill_in_pt ] );
            //handshake to external function in ui.driver_thread
            trans_count_in = 0;

            FDV_TX_fill_in_pt++;
            FDV_TX_fill_in_pt %= FDV_BUFFER_IQ_NUM;
        }

        // if we run out  of buffers lately
        // we wait for availability of at least 2 buffers
        // so that in theory we have uninterrupt flow of audio
        // albeit with a delay of 80ms
        if (out_buffer == NULL && fdv_audio_has_data() > 1)
        {
            fdv_audio_buffer_peek ( &out_buffer );
        }

        if (out_buffer != NULL) // freeDV encode has finished (running in ui_driver.c)?
        {
            // Best thing here would be to use the arm_fir_decimate function! Why?
            // --> we need phase linear filters, because we have to filter I & Q and preserve their phase relationship
            // IIR filters are power saving, but they do not care about phase, so useless at this point
            // FIR filters are phase linear, but need processor power
            // so we now use the decimation function that upsamples like the code below, BUT at the same time filters
            // (and the routine knows that it does not have to multiply with 0 while filtering: if we do upsampling and subsequent
            // filtering, the filter does not know that and multiplies with zero 5 out of six times --> very inefficient)
            // BUT: we cannot use the ARM function, because decimation factor (6) has to be an integer divide of
            // block size (which is 64 in our case --> 64 / 6 = non-integer!)


#if 0	    // activate IIR FIlter

            arm_fill_f32(0,dst,blockSize);

            // UPSAMPLING [by hand]
            for (int j = 0; j < blockSize; j++) //  now we are doing upsampling by 6
            {
                if (modulus_MOD == 0) // put in sample pair
                {
                    dst[j] = out_buffer->samples[outbuff_count]; // + (sample_delta.real * (float32_t)modulus_MOD);
                    //sample_delta = (out_buffer->samples[outbuff_count]-last_sample)/6;
                }

                //adb.a_buffer[1][j] = out_buffer->samples[outbuff_count] + sample_delta*(float32_t)modulus_MOD;


#if 0
                else // in 5 of 6 cases just stuff in zeros = zero-padding / zero-stuffing
                    // or keep the last sample value - like a sample an' old
                {
                    //adb.a_buffer[1][j] = 0;
                    dst[j] = out_buffer->samples[outbuff_count];
                }
#endif

                (int16_t*) modulus_MOD++;
                if (modulus_MOD == 6)
                {
                    // last_sample = out_buffer->samples[outbuff_count];
                    (int16_t*) outbuff_count++;
                    modulus_MOD = 0;
                }
            }

            // Add interpolation filter here to suppress alias frequencies
            // we are upsampling from 8kHz to 48kHz, so we have to suppress all frequencies below 4kHz
            // our FreeDV signal is here already an reconstructed Audio Signal,
            // so a lowpass or bandpass filter with cutoff frequency 50/2800Hz should be fine!

            // This is a temporary installed - neutral - RX Audio Filter which will be replaced by a better
            // hopefully a polyphase interpolation filter - has to be adopted to our upsampling rate and buffersizes.....

            // Filter below uses the TX-"Tenor" filter shape for the RX upsampling filter

// N96K B4: run the FreeDV RX audio filter in the opt-in FreeDV build as well.
#if !defined(IS_SMALL_BUILD) || defined(USE_FREEDV)      // pour reduire la memoire RAM en version Small
            arm_iir_lattice_f32_Nizar ( &IIR_FreeDV_RX_Filter, dst, dst, blockSize );
#endif

            //AudioDriver_tx_filter_audio(true, false, false, dst, dst, blockSize);


        }
        else
        {
            profileEvent(FreeDVTXUnderrun);
            // in case of underrun -> produce silence
            arm_fill_f32( 0, dst, blockSize );
        }

        if (outbuff_count >= FDV_BUFFER_SIZE)
        {
            outbuff_count = 0;
            fdv_audio_buffer_remove( &out_buffer );
            // ok, this one is done with
            out_buffer = NULL;
            fdv_audio_buffer_peek( &out_buffer );
            // we may or may not get a buffer here
            // if not and we have a stall the code somewhere up
            // produces silence until 2 out buffers are available

        }

#else
        for ( int j=0; j < blockSize; j++) //upsampling with integrated interpolation-filter for M=6
            // avoiding multiplications by zero within the arm_iir_filter
        {
            if (outbuff_count >=0)  // here we are not at an block-overlapping region
            {
                dst[j]=
                        Fir_Rx_FreeDV_Interpolate_Coeffs [ 5 -mod_count ]*out_buffer->samples[ outbuff_count   ] +
                        Fir_Rx_FreeDV_Interpolate_Coeffs [ 11-mod_count ]*out_buffer->samples[ outbuff_count+1 ] +
                        Fir_Rx_FreeDV_Interpolate_Coeffs [ 17-mod_count ]*out_buffer->samples[ outbuff_count+2 ] +
                        Fir_Rx_FreeDV_Interpolate_Coeffs [ 23-mod_count ]*out_buffer->samples[ outbuff_count+3 ];
                // here we are actually calculation the interpolation for the current "up"-sample
            }
            else
            {
                //we are at an overlapping region and have to take care of history
                if (outbuff_count == -3)
                {
                    dst[j] =
                            Fir_Rx_FreeDV_Interpolate_Coeffs[5 -mod_count] * History[0] +
                            Fir_Rx_FreeDV_Interpolate_Coeffs[11-mod_count] * History[1] +
                            Fir_Rx_FreeDV_Interpolate_Coeffs[17-mod_count] * History[2] +
                            Fir_Rx_FreeDV_Interpolate_Coeffs[23-mod_count] * out_buffer->samples[0];
                }
                else
                {
                    if (outbuff_count == -2)
                    {
                        dst[j] =
                                Fir_Rx_FreeDV_Interpolate_Coeffs[5 -mod_count] * History[1] +
                                Fir_Rx_FreeDV_Interpolate_Coeffs[11-mod_count] * History[2] +
                                Fir_Rx_FreeDV_Interpolate_Coeffs[17-mod_count] * out_buffer->samples[0] +
                                Fir_Rx_FreeDV_Interpolate_Coeffs[23-mod_count] * out_buffer->samples[1];
                    }
                    else
                    {
                        dst[j] =
                                Fir_Rx_FreeDV_Interpolate_Coeffs[5 -mod_count] * History[2] +
                                Fir_Rx_FreeDV_Interpolate_Coeffs[11-mod_count] * out_buffer->samples[0] +
                                Fir_Rx_FreeDV_Interpolate_Coeffs[17-mod_count] * out_buffer->samples[1] +
                                Fir_Rx_FreeDV_Interpolate_Coeffs[23-mod_count] * out_buffer->samples[2];
                    }
                }
            }


            mod_count++;
            if (mod_count==6)
            {
                outbuff_count++;
                mod_count=0;
            }
        }
    }
    else
    {
        profileEvent(FreeDVTXUnderrun);
        // in case of underrun -> produce silence
        arm_fill_f32 ( 0, dst, blockSize );
    }

    // we used now FDV_BUFFER_SIZE samples (3 from History[], plus FDV_BUFFER_SIZE -3 from out_buffer->samples[])
    if (outbuff_count == (FDV_BUFFER_SIZE-3))//  -3???
    {
        outbuff_count=-3;

        History[0] = out_buffer->samples[FDV_BUFFER_SIZE-3]; // here we have to save historic samples
        History[1] = out_buffer->samples[FDV_BUFFER_SIZE-2]; // to calculate the interpolation in the
        History[2] = out_buffer->samples[FDV_BUFFER_SIZE-1]; // block overlapping region

        // ok, let us free the old buffer
        fdv_audio_buffer_remove(&out_buffer);
        out_buffer = NULL;
        fdv_audio_buffer_peek(&out_buffer);

    }

#endif  //activate FIR Filter
}
return true;
}
#endif

// RTTY Experiment based on code from the DSP Tutorial at http://dp.nonoo.hu/projects/ham-dsp-tutorial/18-rtty-decoder-using-iir-filters/
// Used with permission from Norbert Varga, HA2NON under GPLv3 license
#ifdef USE_RTTY_PROCESSOR
static void AudioDriver_RxProcessor_Rtty(float32_t * const src, int16_t blockSize)
{

    for (uint32_t idx = 0; idx < blockSize; idx++)
    {
        RttyDecoder_ProcessSample(src[idx]);
    }
}
#endif

static void AudioDriver_RxProcessor_Bpsk(float32_t * const src, int16_t blockSize)
{

    for (uint32_t idx = 0; idx < blockSize; idx++)
    {
        BpskDecoder_ProcessSample(src[idx]);
    }
}


//******************************************************************************************************

static float32_t  gain_statique_AGC = 1;


void  AudioDriver_SetupAgcWdsp()
{
    // N96K P2: backport of upstream cfbaae73 (#1730, "AGC values are not restored").
    // All user adjustable AGC parameters are kept in ts.agc_wdsp_* and are loaded from EEPROM before
    // AudioDriver_Init() calls this function. They must NOT be reset to hard-coded defaults here,
    // otherwise a reboot / parameter change would silently replace the user settings (the defect
    // fixed by cfbaae73 in upstream's audio_agc.c). Only the non-persistent run-time state of the AGC
    // (ring buffers, indices, derived multipliers) is initialised in this function.
    static    bool   initialised         = false;
	float32_t        tmp;
    float32_t        sample_rate         = IQ_SAMPLE_RATE_F / (float32_t)ads.decimation_rate;  // 12000= 48000/4
    static    uchar  decimation_rate_old = 0; // will be set to current decimation rate in first round
    // Unlock - re-enable filtering

    ads.af_disabled++;
    ts.dsp_inhibit++;

    // this is a quick and dirty hack
    // it initialises the AGC variables once again,
    // if the decimation rate is changed
    // this should prevent confusion between the distance of in_index and out_index variables
    // because these are freshly initialised
    // in_index and out_index have a distance of 48 (sample rate 12000) or 96 (sample rate 24000)
    // so that has to be defined very well when filter from 4k8 to 5k0 (changing decimation rate from 4 to 2)
    if(decimation_rate_old != ads.decimation_rate)
    {
    	initialised = false; // force initialisation
    	decimation_rate_old = ads.decimation_rate; // remember decimation rate for next time
    }
    // Start variables taken from wdsp
    // RXA.c !!!!
    /*
    0.001,                      // tau_attack
    0.250,                      // tau_decay
    4,                        // n_tau
    10000.0,                    // max_gain
    1.5,                      // var_gain
    1000.0,                     // fixed_gain
    1.0,                      // max_input
    1.0,                      // out_target
    0.250,                      // tau_fast_backaverage
    0.005,                      // tau_fast_decay
    5.0,                      // pop_ratio
    1,                        // hang_enable
    0.500,                      // tau_hang_backmult
    0.250,                      // hangtime
    0.250,                      // hang_thresh
    0.100);                     // tau_hang_decay
     */
    // one time initialization
    if(!initialised)
    {

    	/*
    	 *
    	 * 	//assign constants
	a->ring_buffsize = RB_SIZE;
	//do one-time initialization
	a->out_index = -1;
	a->ring_max = 0.0;
	a->volts = 0.0;
	a->save_volts = 0.0;
	a->fast_backaverage = 0.0;
	a->hang_backaverage = 0.0;
	a->hang_counter = 0;
	a->decay_type = 0;
	a->state = 0;
	a->ring = (double *)malloc0(RB_SIZE * sizeof(complex));
	a->abs_ring = (double *)malloc0(RB_SIZE * sizeof(double));
loadWcpAGC(a);
    	 *
    	 *
    	 * */

    	agc_wdsp.ring_buffsize    = AGC_WDSP_RB_SIZE; // 96  // 192;
		//do one-time initialization
    	agc_wdsp.out_index        = -1;
    	agc_wdsp.fixed_gain       = 1.0;
    	agc_wdsp.ring_max         = 0.0;
    	agc_wdsp.ring_max_avg     = 0.0;
    	agc_wdsp.save_volts       = 0.0;
		agc_wdsp.fast_backaverage = 0.0;
		agc_wdsp.hang_backaverage = 0.0;
		agc_wdsp.hang_counter     = 0;
		agc_wdsp.decay_type       = 0;
		agc_wdsp.state            = 0;

		for(int idx               = 0; idx < AGC_WDSP_RB_SIZE; idx++)
		{
			agc_wdsp.ring    [idx ]  =  0.0;
			agc_wdsp.abs_ring[idx]   =  0.0;
		}



	    agc_wdsp.tau_attack = 0.001;               // tau_attack
	    //    tau_decay = ts.agc_wdsp_tau_decay / 1000.0; // 0.250;                // tau_decay
	    agc_wdsp.n_tau      = 4;                  // n_tau

	    //    max_gain = 1000.0; // 1000.0; determines the AGC threshold = knee level
	    //  max_gain is powf (10.0, (float32_t)ts.agc_wdsp_thresh / 20.0);
	    //    fixed_gain = ads.agc_rf_gain; //0.7; // if AGC == OFF, this gain is used

	    //  if (ts.show_debug_info) { agc_wdsp.max_input = (float32_t) 4096; }
	    //  else  					{ agc_wdsp.max_input = (float32_t) ADC_CLIP_WARN_THRESHOLD;}  // which is 4096 at the moment
	    agc_wdsp.max_input            =  4700;  // 4500.0; // (float32_t) ADC_CLIP_WARN_THRESHOLD; // NIZZ  which is 4096 at the moment
	    //32767.0; // maximum value of 16-bit audio //  1.0; //

	    // if (ts.show_debug_info) { agc_wdsp.out_targ  = (float32_t) 4096;}
	    // else                    { agc_wdsp.out_targ  = (float32_t) ADC_CLIP_WARN_THRESHOLD;} // 4096, tweaked, so that volume when switching between the two AGCs remains equal

	    agc_wdsp.out_targ             =  4700;   // 4500.0;   /// (float32_t) ADC_CLIP_WARN_THRESHOLD; // NIZZZ
	    //12000.0; // target value of audio after AGC


	    agc_wdsp.tau_fast_backaverage =  0.250;          // tau_fast_backaverage
	    agc_wdsp.tau_fast_decay       =  0.005;          // tau_fast_decay
	    agc_wdsp.pop_ratio            =  5.0;            // pop_ratio
	    //    hang_enable             =  0;              // hang_enable
	    agc_wdsp.tau_hang_backmult    =  0.500;          // tau_hang_backmult

	    initialised                   = true;
    }
    //    var_gain     = 32.0;  // slope of the AGC --> this is 10 * 10^(slope / 20) --> for 10dB slope, this is 30.0
    agc_wdsp.var_gain  =  powf (10.0, ((float32_t)ts.agc_wdsp_slope -10 ) / 200.0 ); // 10^(slope / 200)
    // N96K P2: keep the cached float value of the persisted AGC "Clarity" setting in sync, it is
    // the AGC peak/median coefficient used by AudioDriver_RxAgcWdsp() (cfbaae73, #1730).
    ts.float_agc_wdsp_pop_thres = 4.00 + 0.01 * ts.agc_wdsp_pop_thres;

    //    hangtime     = 0.250;                // hangtime
    agc_wdsp.hangtime  = (float32_t) ts.agc_wdsp_hang_time / 1000.0;
    //    hang_thresh  = 0.250;             // hang_thresh

    //  tau_hang_decay = 0.100;          // tau_hang_decay

    //calculate internal parameters
    if ( ts.agc_wdsp_switch_mode )
    {
        switch ( ts.agc_wdsp_mode )
        {
			case 5: //agcOFF
					break;

			case 1: //agc_LONG
					agc_wdsp.hangtime = 2.000;
					//      ts.agc_wdsp_tau_decay = 2000;
					//      hang_thresh = 1.0;
					//      ts.agc_wdsp_hang_enable = 1;
					break;

			case 2: //agc_SLOW
					agc_wdsp.hangtime = 1.000;
					//      hang_thresh = 1.0;
					//      ts.agc_wdsp_tau_decay = 500;
					//      ts.agc_wdsp_hang_enable = 1;
					break;

			case 3: //agc_MED
					//      hang_thresh = 1.0;
					agc_wdsp.hangtime = 0.250;
					//      ts.agc_wdsp_tau_decay = 250;
					break;

			case 4: //agc_FAST
					//    hang_thresh = 1.0;
					agc_wdsp.hangtime = 0.100;
					//      ts.agc_wdsp_tau_decay = 50;
					break;

			case 0: //agc_Frank --> very long
					//    ts.agc_wdsp_hang_enable = 0;
					//    hang_thresh             = 0.300; // from which level on should hang be enabled
					agc_wdsp.hangtime             = 3.000; // hang time, if enabled
					agc_wdsp.tau_hang_backmult    = 0.500; // time constant exponential averager
					//     ts.agc_wdsp_tau_decay  = 4000; // time constant decay long
					agc_wdsp.tau_fast_decay       = 0.05;          // tau_fast_decay
					agc_wdsp.tau_fast_backaverage = 0.250; // time constant exponential averager
					break;

			default:
					break;
        }
        ts.agc_wdsp_switch_mode = 0;
    }
    //  float32_t noise_offset = 10.0 * log10f(fhigh - rxa[channel].nbp0.p->flow)
    //          * size / rate);
    //  max_gain               = out_target / var_gain * powf (10.0, (thresh + noise_offset) / 20.0));

    agc_wdsp.tau_hang_decay    = (float32_t) ts.agc_wdsp_tau_hang_decay / 1000.0;
    agc_wdsp.tau_decay         = (float32_t) ts.agc_wdsp_tau_decay[ts.agc_wdsp_mode] / 1000.0;
    agc_wdsp.max_gain          = powf (10.0, (float32_t)ts.agc_wdsp_thresh / 20.0);
    agc_wdsp.fixed_gain        = agc_wdsp.max_gain / 10.0;

    // attack_buff_size is 48 for sample rate == 12000 and
    // 96 for sample rate     == 24000


    agc_wdsp.attack_buffsize   = 48; // 4ms //  (int) ceil(sample_rate * agc_wdsp.n_tau * agc_wdsp.tau_attack); // 48 = 12000 * 4 * 0.001

    agc_wdsp.in_index          = agc_wdsp.out_index  + 48 ; //  + 46 ;  // agc_wdsp.attack_buffsize; // out_index + attack_buffsize can be more than  ring_bufsize !!!
    agc_wdsp.in_index         %= agc_wdsp.ring_buffsize; // N96K NIZ-fix H6: was a hard-coded 96 (trap if AGC_WDSP_RB_SIZE changes); need to keep this within the index boundaries

    agc_wdsp.attack_mult       = ((float32_t)ts.agc_wdsp_Attackk)/1000.0;  // 0.05 ;    //  0.08    // 1.0 - expf(-1.0 / (sample_rate * agc_wdsp.tau_attack));


 //   agc_wdsp.decay_mult        = 1.0 - expf(-1.0 / (sample_rate * agc_wdsp.tau_decay));       // original

    agc_wdsp.decay_mult        = 1.0/(12.0 *(float32_t) ts.agc_wdsp_tau_decay[ts.agc_wdsp_mode] ); // 12   NIZZZ

    agc_wdsp.fast_decay_mult   = 1.0 - expf(-1.0 / (sample_rate * agc_wdsp.tau_fast_decay));

    agc_wdsp.fast_backmult     = 1.0 - expf(-1.0 / (sample_rate * agc_wdsp.tau_fast_backaverage)); //
    agc_wdsp.onemfast_backmult = 1.0 - agc_wdsp.fast_backmult;

    agc_wdsp.out_target        = agc_wdsp.out_targ * 0.98158; // agc_wdsp.out_targ * (1.0 - expf(-(float32_t)agc_wdsp.n_tau)) * 0.9999; //
    //  out_target             = out_target * (1.0 - expf(-(float32_t)n_tau)) * 0.9999;
    agc_wdsp.min_volts         = agc_wdsp.out_target / ( agc_wdsp.var_gain * agc_wdsp.max_gain );

 //   if (agc_wdsp.min_volts < 1 )    agc_wdsp.min_volts = 1;  /// NIZZZ Limitation du niveau de seuil le plus bas ce qui correspond à 72db deja pas mal

    agc_wdsp.inv_out_target    = 1.0 / agc_wdsp.out_target;

    tmp                        = log10f( agc_wdsp.out_target / (agc_wdsp.max_input * agc_wdsp.var_gain * agc_wdsp.max_gain));
    if (tmp == 0.0) {    tmp   = 1e-16;  }
    agc_wdsp.slope_constant    = (agc_wdsp.out_target * (1.0 - 1.0 / agc_wdsp.var_gain)) / tmp;

    agc_wdsp.inv_max_input     = 1.0 / agc_wdsp.max_input;

    if (agc_wdsp.max_input     > agc_wdsp.min_volts)
    {
        float32_t convert      = powf (10.0, (float32_t)ts.agc_wdsp_hang_thresh / 20.0);
        tmp                    = (convert - agc_wdsp.min_volts) / (agc_wdsp.max_input - agc_wdsp.min_volts);
        if (tmp < 1e-8) { tmp  = 1e-8; }
        agc_wdsp.hang_thresh   = 1.0 + 0.125 * log10f (tmp);
    }
    else
    {
        agc_wdsp.hang_thresh   = 1.0;
    }

    tmp                        = powf (10.0, (agc_wdsp.hang_thresh - 1.0) / 0.125);
    agc_wdsp.hang_level        = (agc_wdsp.max_input * tmp + (agc_wdsp.out_target / (agc_wdsp.var_gain * agc_wdsp.max_gain)) * (1.0 - tmp)) * 0.637;

    agc_wdsp.hang_backmult     = 1.0 - expf(-1.0 / ( sample_rate * agc_wdsp.tau_hang_backmult ));
    agc_wdsp.onemhang_backmult = 1.0 - agc_wdsp.hang_backmult;

    agc_wdsp.hang_decay_mult   = 1.0 - expf( -1.0 / ( sample_rate * agc_wdsp.tau_hang_decay ));

    			//********************************************************************************
    			//***********************    AGC  Nizar ******************************************
    			//********************************************************************************

    float32_t   max_inp              =  5000.0;    // Maximum input signal to the AGC function, to have maximum output level
    float32_t   max_out              =  5850.0;    // 5300 // Maximum target output level of the AGC function
                agc_wdsp.thresh_inp  =  0.00001;    // max_inp * powf ( 10, -(float32_t)ts.agc_wdsp_thresh / 20.0 );  // seuil du signal d'entrée
                // if (agc_wdsp.thresh_inp < 1.0) { agc_wdsp.thresh_inp = 1.0; }  // limitation à 78db de gain
                agc_wdsp.thresh_out  =  max_out * powf ( 10, (-(float32_t)ts.agc_wdsp_slope +10 )  /200.0 );  // gain au seuil du signal d'entrée
                agc_wdsp.pente       =  ( max_out - agc_wdsp.thresh_out ) / ( max_inp); //  - agc_wdsp.thresh_inp );

               // gain_statique_AGC    =  (float32_t) ts.agc_wdsp_thresh ;



     // Unlock - re-enable filtering
       if  ( ts.dsp_inhibit  ) {  ts.dsp_inhibit--;  }
       if  ( ads.af_disabled ) {  ads.af_disabled--; }

}

///*****************************************************************************************
/// *******************************  NIZAR  RX  AGC  Function  *****************************
///*****************************************************************************************
//   static         float32_t  crete_coeff       = 4.90;  // 5.35;    // 5.25;     // original = 5.0  // 5.15;
//   static  const  float32_t  ffast_decay_mult  = 0.00600; // 0.0160;  // original = 0.016528  ( 0.009 bon pour ne pas trop distordre les 100Hz )
static  const  float32_t  ffast_decay_mult  = 0.00600; // 0.0160;  // original = 0.016528  ( 0.009 bon pour ne pas trop distordre les 100Hz )


void   AudioDriver_RxAgcWdsp ( const uint16_t  blockSize, float32_t  *AGC_buffer )
{


	        const float32_t   crete_coeff =  ts.float_agc_wdsp_pop_thres;

    //**********************************************************************************************************
    //******************************************  AGC ON *******************************************************
    //**********************************************************************************************************

	const uint32_t     AGC_Buffer_Size_96  = AGC_WDSP_RB_SIZE; // N96K NIZ-fix H6: keep the wrap-around in sync with the ring buffer size
	const uint16_t     Out_inp_sheeft      = 48 ; // + 46;    // original  48
	const float32_t    coeff_moyennage     = 0.000333;   // original  0.0003332  equivalent de moyennage sur 250ms
	const float32_t    zzero               = 0.0 ;
	const float32_t    max_output          = 7000; // 7600;
	static uint16_t    indica_tempo_rapide = 0;

/*	if ( ts.rx_tx_switch_accentuation )
	{
		ts.rx_tx_switch_accentuation = false;
		if ( ts.dmod_mode != DEMOD_CW )  { agc_wdsp.ring_max_avg*=0.75;} else agc_wdsp.ring_max_avg *= 2.0;
	} */



    for ( uint16_t  i = 0; i < blockSize; i++ )       // Traitement par echantillons un à un
    {

					//*****************************************************************************************
					//****************  Incrementer les indices entree / sortie avec rebouclage  **************
					//*****************************************************************************************
        if (++agc_wdsp.out_index >= AGC_Buffer_Size_96)    {  agc_wdsp.out_index   -= AGC_Buffer_Size_96;  }
        if (++agc_wdsp.in_index  >= AGC_Buffer_Size_96)    {  agc_wdsp.in_index    -= AGC_Buffer_Size_96;  }


					 //****************************************************************************************
					 //**************************  recuperer les  valeurs précédentes  **********************
					 //*****************************************************************************************

        agc_wdsp.out_sample                    =  agc_wdsp.ring     [ agc_wdsp.out_index ];
        agc_wdsp.abs_out_sample                =  agc_wdsp.abs_ring [ agc_wdsp.out_index ];

					//****************************************************************************************************
					//************************  Sauvegarde des nouvelles valeurs d'entrees ********************************
					//****************************************************************************************************

        agc_wdsp.ring    [ agc_wdsp.in_index ] =         AGC_buffer [i]  ;   // retard de 192 echantillons = 16 ms
        agc_wdsp.abs_ring[ agc_wdsp.in_index ] =  fabsf( AGC_buffer [i] );



        				//*********************************************************************************
        				//********* Moyennage  de l'absolue  du signal sur une trainéee de 3000 ech *******
                        //*********************************************************************************

       agc_wdsp.fast_backaverage += ( agc_wdsp.abs_ring [ agc_wdsp.out_index ]  - agc_wdsp.fast_backaverage) * coeff_moyennage ;  //0.000333  sur 250 ms de moyennage


        				//*********************************************************************************
        				//****** Calculer le max des prochains 48 ech à la baisse du signal ***************
        				//*********************************************************************************

        if  ( agc_wdsp.abs_out_sample >= agc_wdsp.ring_max )
        {
            agc_wdsp.ring_max =  zzero;                 // pour recalculer le ring_max de nouveau sur tout l'interval qui suit des 48  abs ( échantillons)
            int             k = agc_wdsp.out_index;    // indice du depart de comparaison

            for ( uint16_t  j = 0;  j < Out_inp_sheeft ;  j++ )    // agc_wdsp.attack_buffsize, interval de 4 ms = (48+1) s / 12000
            {
            	if ( ++k == AGC_Buffer_Size_96 ) { k = 0; }
                if ( agc_wdsp.abs_ring[k] >  agc_wdsp.ring_max )  {  agc_wdsp.ring_max = agc_wdsp.abs_ring[k]; }
            }
        }

    				  //**********************************************************************************
    				  //******  Calcul de la moyenne du Max selon la dynamique et l'allure du signal ****                                              ***************
    				  //**********************************************************************************
        if ( agc_wdsp.ring_max >= agc_wdsp.ring_max_avg )    // Moyennage rapide durant l'ATTACK
		{
				agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.attack_mult; // 0.040 attack reglable au menu au max 0.090  (0.08=1ms à 0.04=2ms , 0.02=4ms)
		}
		else                       // pour retour rapide en cas de bruit impulsionnel qui reduit le gain
		{
			if ( (indica_tempo_rapide > 4) && (agc_wdsp.ring_max_avg > (crete_coeff * agc_wdsp.fast_backaverage) ))  // Moyennage Mediane : donne plus de dynamique quand il y a un signal audio
			{
				agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * ffast_decay_mult;  // 0.0165 (6ms) //  0.009; (10 ms)  moyennage rapide
				// ts.peak_number++;
				indica_tempo_rapide = 0;
			}
			else       //  long time constant detector : Moyennage Long au relachement complet du signal
			{
				agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.decay_mult;      // 0.00021   moyennage long pour 400ms decay selected setting

			}
		}



        ///*****************************************************************************************
        ///**********************************  AGC  Nizar ******************************************
        ///*****************************************************************************************

        if   ( agc_wdsp.ring_max_avg < agc_wdsp.thresh_inp ) { agc_wdsp.ring_max_avg = agc_wdsp.thresh_inp;   } // no AGC action is taking place


        float32_t  desired_out   = agc_wdsp.pente * ( agc_wdsp.ring_max_avg /*- agc_wdsp.thresh_inp*/ ) + agc_wdsp.thresh_out; // Pente lineaire et non Logarithmique

        // float32_t   final_gain    = desired_out / agc_wdsp.ring_max_avg; // gain fixe au dessous de thresh_inp
        if ( desired_out > max_output ) { desired_out = max_output; }

        AGC_buffer[i] = agc_wdsp.out_sample * desired_out / agc_wdsp.ring_max_avg ;
    }


    //    ts.agc_wdsp_action = 1; // LED indicator for AGC action
  //  if ( agc_wdsp.ring_max_avg > 880 )  { crete_coeff = 5.35; ts.agc_wdsp_Deep = false;} else { crete_coeff = 4.90; ts.agc_wdsp_Deep = true;}

    indica_tempo_rapide += 1;
}

//*************************************************************************************************************





///*****************************************************************************************
/// *******************************  Original RX  AGC  Function  ************************************
///*****************************************************************************************

/*

void   AudioDriver_RxAgcWdsp ( uint16_t  blockSize, float32_t  *AGC_buffer )
{

    if (ts.agc_wdsp_mode == 5)                       // AGC OFF
    {
        for ( uint16_t i = 0; i < blockSize; i++  )
        {
            AGC_buffer   [i] = AGC_buffer  [i] * agc_wdsp.fixed_gain;
        }
        return;
    }

    //**********************************************************************************************************
    //******************************************  AGC ON *******************************************************
    //**********************************************************************************************************

    for ( uint16_t  i = 0; i < blockSize; i++ )       // Traitement par echantillons un à un
    {
        if (++agc_wdsp.out_index >= agc_wdsp.ring_buffsize)    {  agc_wdsp.out_index   -= agc_wdsp.ring_buffsize;  }
        if (++agc_wdsp.in_index  >= agc_wdsp.ring_buffsize)    {  agc_wdsp.in_index    -= agc_wdsp.ring_buffsize;  }


				 //****************************************************************************************
				 //**************************  recupération des  valeurs précédentes  **********************
				 //*****************************************************************************************

        agc_wdsp.out_sample                        =  agc_wdsp.ring     [     agc_wdsp.out_index ];
        agc_wdsp.abs_out_sample                    =  agc_wdsp.abs_ring [     agc_wdsp.out_index ];

        		//****************************************************************************************************
        		//************************  Sauvegarde des nouvelles valeurs d'entrés ********************************
        		//****************************************************************************************************

        agc_wdsp.ring    [     agc_wdsp.in_index ] =        AGC_buffer[i];
        agc_wdsp.abs_ring[     agc_wdsp.in_index ] =  fabsf(AGC_buffer[i]);



        				//*********************************************************************************
        				//****************  Calcul de la moyenne rapide ***********************************
                        //*********************************************************************************
        agc_wdsp.fast_backaverage =  agc_wdsp.fast_backmult * agc_wdsp.abs_out_sample + agc_wdsp.onemfast_backmult * agc_wdsp.fast_backaverage;



        				//*********************************************************************************
                		//****************  Calcul de la moyenne Lente  ***********************************
                        //*********************************************************************************
        agc_wdsp.hang_backaverage =  agc_wdsp.hang_backmult * agc_wdsp.abs_out_sample + agc_wdsp.onemhang_backmult * agc_wdsp.hang_backaverage;




        if   ( agc_wdsp.hang_backaverage > agc_wdsp.hang_level )  {  ts.agc_wdsp_hang_action = 1;  }
        else        										      {  ts.agc_wdsp_hang_action = 0;  }



        if ((agc_wdsp.abs_out_sample >= agc_wdsp.ring_max) && (agc_wdsp.abs_out_sample > 0.0))
        {
            agc_wdsp.ring_max = 0.0;

            int             k = agc_wdsp.out_index;

            for ( uint16_t j = 0;  j < agc_wdsp.attack_buffsize;  j++ )
            {
                if ( ++k                  == agc_wdsp.ring_buffsize )  {  k = 0;                                    }
                if ( agc_wdsp.abs_ring[k] >  agc_wdsp.ring_max      )  {  agc_wdsp.ring_max = agc_wdsp.abs_ring[k]; }
            }
        }




        if (agc_wdsp.abs_ring[agc_wdsp.in_index] > agc_wdsp.ring_max)  {  agc_wdsp.ring_max = agc_wdsp.abs_ring[agc_wdsp.in_index];  }


        if (agc_wdsp.hang_counter > 0)  {  --agc_wdsp.hang_counter;  }

        switch ( agc_wdsp.state )
        {
			case 0: // starting point after ATTACK
					{
						if (agc_wdsp.ring_max >= agc_wdsp.ring_max_avg)    // ATTACK
						{
							agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.attack_mult;
						}

						else                                                 // DECAY
						{
							if (agc_wdsp.ring_max_avg > agc_wdsp.pop_ratio * agc_wdsp.fast_backaverage)
							{ 						//  short time constant detector
								agc_wdsp.state  = 1;
								agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.fast_decay_mult;
							}
							else
							{                       //  hang AGC enabled and being activated
								if (ts.agc_wdsp_hang_enable  && ( agc_wdsp.hang_backaverage > agc_wdsp.hang_level) )
								{
									agc_wdsp.state        = 2;
									agc_wdsp.hang_counter = (int) ( agc_wdsp.hangtime * IQ_SAMPLE_RATE_F / ads.decimation_rate );
									agc_wdsp.decay_type   = 1;
								}
								else
								{                   //  long time constant detector
									agc_wdsp.state           = 3;
									agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.decay_mult;
									agc_wdsp.decay_type      = 0;
								}
							}
						}
						break;
					}


			case 1:            				// short time constant decay
					{
						if ( agc_wdsp.ring_max >= agc_wdsp.ring_max_avg )       // ATTACK
						{
							agc_wdsp.state           = 0;
							agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.attack_mult;
						}
						else
						{
							if ( agc_wdsp.ring_max_avg  > agc_wdsp.save_volts )  // short time constant detector// short time constant detector
							{
								 agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.fast_decay_mult;
							}
							else
							{
								if (agc_wdsp.hang_counter > 0)
								{
									agc_wdsp.state = 2;
								}
								else
								{
									if ( agc_wdsp.decay_type == 0 )          // long time constant detector
									{
										agc_wdsp.state           = 3;
										agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.decay_mult;
									}
									else                                     // hang time constant
									{
										agc_wdsp.state           = 4;
										agc_wdsp.ring_max_avg += ( agc_wdsp.ring_max - agc_wdsp.ring_max_avg ) * agc_wdsp.hang_decay_mult;
									}
								}
							}
						}
						break;
					}


			case 2: 				// Hang is enabled and active, hang counter still counting
					{ // ATTACK

						if (agc_wdsp.ring_max       >= agc_wdsp.ring_max_avg)
						{
							agc_wdsp.state           = 0;
							agc_wdsp.save_volts      = agc_wdsp.ring_max_avg;
							agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.attack_mult;
						}
						else
						{
							if ( agc_wdsp.hang_counter  == 0 )
							{ 									// hang time constant
								agc_wdsp.state           = 4;
								agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.hang_decay_mult;
							}
						}
						break;
					}


			case 3: 					// long time constant decay in progress
					{
						if ( agc_wdsp.ring_max      >= agc_wdsp.ring_max_avg )   // ATTACK
						{
							agc_wdsp.state           = 0;
							agc_wdsp.save_volts      = agc_wdsp.ring_max_avg;
							agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.attack_mult;
						}
						else                                                     // DECAY
						{
							agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.decay_mult;
						}
						break;
					}



			case 4: // hang was enabled and counter has counted to zero --> hang decay
					{
						if (agc_wdsp.ring_max       >= agc_wdsp.ring_max_avg)     // ATTACK
						{
							agc_wdsp.state           = 0;
							agc_wdsp.save_volts      = agc_wdsp.ring_max_avg;
							agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.attack_mult;
						}

						else                                                        // HANG DECAY
						{
							agc_wdsp.ring_max_avg += (agc_wdsp.ring_max - agc_wdsp.ring_max_avg) * agc_wdsp.hang_decay_mult;
						}
						break;
					}
        }



        if          ( agc_wdsp.ring_max_avg < agc_wdsp.min_volts )
        			{ agc_wdsp.ring_max_avg = agc_wdsp.min_volts;  ts.agc_wdsp_action = 0; } // no AGC action is taking place
        else		{                                              ts.agc_wdsp_action = 1; } // LED indicator for AGC action


        float32_t  vo  =  log10f_fast ( agc_wdsp.inv_max_input * agc_wdsp.ring_max_avg );
        if ( vo > 0.0)  { vo = 0.0; }

        float32_t  mult = ( agc_wdsp.out_target - agc_wdsp.slope_constant * vo ) / agc_wdsp.ring_max_avg;
        AGC_buffer[i]   =   agc_wdsp.out_sample * mult;
    }
}
*/





#if 0
//*----------------------------------------------------------------------------
//* Function Name       : audio_rx_agc_processor
//* Object              :
//* Object              : Processor for receiver AGC
//* Input Parameters    :
//* Output Parameters   :
//* Functions called    :
//*----------------------------------------------------------------------------


static  void  AudioDriver_RxAgcProcessor( int16_t  blockSize, float32_t  *agcbuffer )  // ancien modele AGC
{
    static  ulong 		i;
    static  ulong		agc_delay_inbuf = 0,  agc_delay_outbuf = 0;
    static  float32_t    w = 0.0;


    //
    // AGC function - Look-ahead type by KA7OEI, revised September 2015 to eliminate possible low-order, low-frequency instabilities associated with steady-state signals.
    // Note that even though it gets only one AGC value per cycle, it *does* do "psize/2" calculations to iterate out the AGC value more precisely than it would
    // were it called once per DMA cycle.  If it is called once per DMA cycle it may tend to weakly oscillate under certain conditions and possibly overshoot/undershoot.
    //
    // FM does not need AGC and the S-meter reading is calculated within the FM demodulation function and not here.
    //


    if(ts.agc_mode != AGC_OFF)
    {
        for(i = 0; i < blockSize; i++)
        {
            if((ts.dmod_mode == DEMOD_AM))		// if in AM, get the recovered DC voltage from the detected carrier
            { // TODO: cleanup
                // but leave this here for the moment, we are still testing 2017-02-08
                //                ads.agc_calc = ads.am_fm_agc * ads.agc_val;
//                ads.agc_calc = fabs(adb.a_buffer[i]) * ads.agc_val;
                ads.agc_calc = fabs(agcbuffer[i]) * ads.agc_val;
            }
            else	 							// not AM - get the amplitude of the recovered audio
            {   // take the absolute value of the pre-AGC, post-filter audio signal
                // and multiply by the current AGC value
//                ads.agc_calc = fabs(adb.a_buffer[i]) * ads.agc_val;
                ads.agc_calc = fabs(agcbuffer[i]) * ads.agc_val;
                //agc_calc = max_signal * ads.agc_val;	// calculate current level by scaling it with AGC value
            }

            // if the absolute audio value * AGC value is above the AGC knee, reduce AGC value quickly --> attack: fixed at 0.033
            // if below, increase slowly --> decay
            float32_t agc_decay_scaled = (ads.agc_calc < ads.agc_knee) ? (ads.agc_decay * ads.agc_decimation_scaling) : AGC_ATTACK;
            // if agc_calc is lower than knee - Increase gain slowly for AGC DECAY - scale time constant with decimation

            // agc_val   = agc_val + agc_val * ((agc_knee - audio_value * agc_val) / agc_knee ) * agc_decay_scaled;
            ads.agc_var  = ads.agc_knee - ads.agc_calc;	// calculate difference between agc value and "knee" value
            ads.agc_var /= ads.agc_knee;	// calculate ratio of difference between knee value and this value
            ads.agc_val += ads.agc_val * agc_decay_scaled * ads.agc_var; // adjust agc_val

            // lower limit for AGC_val
            if(ads.agc_val <= AGC_VAL_MIN)	// Prevent zero or "negative" gain values
            {
                ads.agc_val = AGC_VAL_MIN;
            }

            // upper limit for AGC_val
            if(ads.agc_val >= ads.agc_rf_gain)	 	// limit AGC to reasonable values when low/no signals present
            {
                ads.agc_val     = ads.agc_rf_gain;
                if(ads.agc_val >= ads.agc_val_max)	// limit maximum gain under no-signal conditions
                {
                    ads.agc_val = ads.agc_val_max;
                }
            }
            adb.agc_valbuf[i] = ads.agc_val;            // store in "running" AGC history buffer for later application to audio data
        }
    }
    else    // AGC Off - manual AGC gain
    {
        ads.agc_val =  ads.agc_rf_gain;          // use logarithmic gain value in RF gain control
        arm_fill_f32 ( ads.agc_rf_gain, adb.agc_valbuf, blockSize );
    }

    // Delay the post-AGC audio slightly so that the AGC's "attack" will very slightly lead the audio being acted upon by the AGC.
    // This eliminates a "click" that can occur when a very strong signal appears due to the AGC lag.  The delay is adjusted based on
    // decimation rate so that it is constant for all settings.

    arm_copy_f32 ( agcbuffer                            , &audio_delay_buffer[agc_delay_inbuf], blockSize);	// put new data into the delay buffer
    arm_copy_f32 ( &audio_delay_buffer[agc_delay_outbuf], agcbuffer                           , blockSize);	// take old data out of the delay buffer

    // Update the in/out pointers to the AGC delay buffer
    agc_delay_inbuf  += blockSize;						// update circular delay buffer
    agc_delay_outbuf  = agc_delay_inbuf + blockSize;
    agc_delay_inbuf  %= ads.agc_delay_buflen;
    agc_delay_outbuf %= ads.agc_delay_buflen;

    // DC elimination AFTER the AGC detection
    // because we need the carrier DC for the calmness and functioning of the AGC
    // DD4WH 2017-02-08

    /*
    if(ts.dmod_mode == DEMOD_AM || ts.dmod_mode == DEMOD_SAM)
    {
    	static float32_t    wold = 0.0;
        // eliminate DC in the audio before application of AGC gain
        for(i = 0; i < blockSize; i++)
        {
            w            = agcbuffer[i] + wold * 0.9999; // yes, I want a superb bass response ;-)
            agcbuffer[i] = w - wold;
            wold         = w;
        }
    }*/

    // Now apply pre-calculated AGC values to delayed audio

    arm_mult_f32( agcbuffer, adb.agc_valbuf, agcbuffer, blockSize );		// do vector multiplication to apply delayed "running" AGC data

}
#endif


//
//
//*----------------------------------------------------------------------------
//* Function Name       : audio_demod_fm
//* Object              : FM demodulator (October, 2015 - KA7OEI)
//* Object              :
//* Input Parameters    : size - size of buffer on which to operate
//* Output Parameters   :
//* Functions called    :
//*----------------------------------------------------------------------------
static void AudioDriver_DemodFM(const int16_t blockSize)
{

	float r, s, angle, x, y, a, b;
	float32_t goertzel_buf[blockSize], squelch_buf[blockSize];
	bool tone_det_enabled;
	static float i_prev, q_prev, lpf_prev, hpf_prev_a, hpf_prev_b;// used in FM detection and low/high pass processing

	static float subdet = 0;			// used for tone detection
	static uchar count  = 0, tdet = 0;  // used for squelch processing and debouncing tone detection, respectively
	static ulong gcount = 0;			// used for averaging in tone detection

	if (ts.iq_freq_mode != FREQ_IQ_CONV_MODE_OFF)// bail out if translate mode is not active
	{

		tone_det_enabled = ts.fm_subaudible_tone_det_select ? 1 : 0;// set a quick flag for checking to see if tone detection is enabled

		for (uint16_t i = 0; i < blockSize; i++)
		{
			// first, calculate "x" and "y" for the arctan2, comparing the vectors of present data with previous data

			y = (i_prev * adb.q_buffer[i]) - (adb.i_buffer[i] * q_prev);
			x = (i_prev * adb.i_buffer[i]) + (adb.q_buffer[i] * q_prev);

			/*        //
			  we do not use this approximation any more, because it does not contribute significantly to saving processor cycles,
			  and does not deliver as clean audio as we would expect.
			  with atan2f the audio (and especially the hissing noise!) is much cleaner, DD4WH

			 // What follows is adapted from "Fixed-Point Atan2 With Self Normalization", public domain code by "Jim Shima".
			 // The result is "approximate" - but plenty good enough for speech-grade communications!
			 //
			 // Do calculation of arc-tangent (with quadrant preservation) of of I and Q channels, comparing with previous sample.
			 // Because the result is absolute (we are using ratios!) there is no need to apply any sort of amplitude limiting
			 //
			 abs_y = fabs(y) + 2e-16;		// prevent us from taking "zero divided by zero" (indeterminate value) by setting this to be ALWAYS at least slightly higher than zero
			 //
			 if(x >= 0)	 					// Quadrant 1 or 4
			 {
			 r = (x - abs_y) / (x + abs_y);
			 angle = FM_DEMOD_COEFF1 - FM_DEMOD_COEFF1 * r;
			 }
			 else	 						// Quadrant 2 or 3
			 {
			 r = (x + abs_y) / abs_y - x;
			 angle = FM_DEMOD_COEFF2 - FM_DEMOD_COEFF1 * r;
			 }
			 //
			 if (y < 0)						// Quadrant 3 or 4 - flip sign
			 {
			 angle = -angle;
			 }

			 */
			angle = atan2f( y, x );

			// we now have our audio in "angle"
			squelch_buf[i] = angle;	// save audio in "d" buffer for squelch noise filtering/detection - done later

			// Now do integrating low-pass filter to do FM de-emphasis
			a = lpf_prev + (FM_RX_LPF_ALPHA * (angle - lpf_prev));	//
			lpf_prev = a;			// save "[n-1]" sample for next iteration

			goertzel_buf[i] = a;	// save in "c" for subaudible tone detection

			if (((!ads.fm_squelched) && (!tone_det_enabled))
					|| ((ads.fm_subaudible_tone_detected) && (tone_det_enabled))
					|| ((!ts.fm_sql_threshold)))// high-pass audio only if we are un-squelched (to save processor time)
			{

				// Do differentiating high-pass filter to attenuate very low frequency audio components, namely subadible tones and other "speaker-rattling" components - and to remove any DC that might be present.
				b          = FM_RX_HPF_ALPHA * (hpf_prev_b + a - hpf_prev_a);// do differentiation
				hpf_prev_a = a;		// save "[n-1]" samples for next iteration
				hpf_prev_b = b;
				//
				adb.a_buffer[0][i] = b;// save demodulated and filtered audio in main audio processing buffer
			}
			else if ((ads.fm_squelched)
					|| ((!ads.fm_subaudible_tone_detected) && (tone_det_enabled)))// were we squelched or tone NOT detected?
			{
				adb.a_buffer[0][i] = 0;// do not filter receive audio - fill buffer with zeroes to mute it
			}

			q_prev = adb.q_buffer[i];// save "previous" value of each channel to allow detection of the change of angle in next go-around
			i_prev = adb.i_buffer[i];
		}
#if 0
		if (!ts.agc_wdsp)
		{
			//
			ads.am_fm_agc = sqrtf(
					(q_prev * q_prev) + (i_prev * i_prev)) * FM_AGC_SCALING;// calculate amplitude of carrier to use for AGC indication only (we need it for nothing else!)
			//
			// Do "AGC" on FM signal:  Calculate/smooth signal level ONLY - no need for audio scaling
			//
			ads.agc_calc = ads.am_fm_agc * ads.agc_val;
			//
			if (ads.agc_calc < ads.agc_knee)// is audio below AGC "knee" value?
			{
				ads.agc_var  = ads.agc_knee - ads.agc_calc;// calculate difference between agc value and "knee" value
				ads.agc_var /= ads.agc_knee;// calculate ratio of difference between knee value and this value
				ads.agc_val += ads.agc_val * AGC_DECAY_FM * ads.agc_var;// Yes - Increase gain for AGC DECAY (always fast in FM)
			}
			else
			{
				ads.agc_var  = ads.agc_calc - ads.agc_knee;// calculate difference between agc value and "knee" value
				ads.agc_var /= ads.agc_knee;// calculate ratio of difference between knee value and this value
				ads.agc_val -= ads.agc_val * AGC_ATTACK_FM * ads.agc_var;// Fast attack to increase attenuation (do NOT scale w/decimation or else oscillation results)
				if (ads.agc_val <= AGC_VAL_MIN)	// Prevent zero or "negative" gain values
				{
					ads.agc_val = AGC_VAL_MIN;
				}
			}
			if (ads.agc_val >= ads.agc_rf_gain)	// limit AGC to reasonable values when low/no signals present
			{
				ads.agc_val = ads.agc_rf_gain;
				if (ads.agc_val >= ads.agc_val_max)	// limit maximum gain under no-signal conditions
				{
					ads.agc_val = ads.agc_val_max;
				}
			}
		}
#endif

		// *** Squelch Processing ***
		arm_iir_lattice_f32_Nizar ( &IIR_Squelch_HPF, squelch_buf, squelch_buf, blockSize);	// Do IIR high-pass filter on audio so we may detect squelch noise energy

		ads.fm_sql_avg = ((1 - FM_RX_SQL_SMOOTHING) * ads.fm_sql_avg)  + (FM_RX_SQL_SMOOTHING * sqrtf(fabsf(squelch_buf[0])));// IIR filter squelch energy magnitude:  We need look at only one representative sample

		// Squelch processing
		//
		// Determine if the (averaged) energy in "ads.fm_sql_avg" is above or below the squelch threshold
		//
		if (count == 0)	// do the squelch threshold calculation much less often than we are called to process this audio
		{
			if (ads.fm_sql_avg > 0.175)	// limit maximum noise value in averaging to keep it from going out into the weeds under no-signal conditions (higher = noisier)
			{
				ads.fm_sql_avg = 0.175;
			}

			b = ads.fm_sql_avg * 250;  //  NIZZ 172;// scale noise amplitude to range of squelch setting

			if (b > 24) 	{ b = 24; }					// limit noise amplitude range
			b = 22 - b;	                 // "invert" the noise power so that high number now corresponds with quieter signal:  "b" may now be compared with squelch setting
			//
			// Now evaluate noise power with respect to squelch setting
			//
			if (!ts.fm_sql_threshold)	 	// is squelch set to zero?
			{
				ads.fm_squelched = false;		// yes, the we are un-squelched
			}
			else if (ads.fm_squelched)	 	// are we squelched?
			{
				if (b >= (float) (ts.fm_sql_threshold + FM_SQUELCH_HYSTERESIS))	// yes - is average above threshold plus hysteresis?
				{
					ads.fm_squelched = false;		//  yes, open the squelch
				}
			}
			else	 	// is the squelch open (e.g. passing audio)?
			{
				if (ts.fm_sql_threshold > FM_SQUELCH_HYSTERESIS)// is setting higher than hysteresis?
				{
					if (b
							< (float) (ts.fm_sql_threshold
									- FM_SQUELCH_HYSTERESIS))// yes - is average below threshold minus hysteresis?
					{
						ads.fm_squelched = true;	// yes, close the squelch
					}
				}
				else	 // setting is lower than hysteresis so we can't use it!
				{
					if (b < (float) ts.fm_sql_threshold)// yes - is average below threshold?
					{
						ads.fm_squelched = true;	// yes, close the squelch
					}
				}
			}
			//
			count++;// bump count that controls how often the squelch threshold is checked
			count &= FM_SQUELCH_PROC_DECIMATION;	// enforce the count limit
		}

		//
		// *** Subaudible tone detection ***
		//
		if (tone_det_enabled)// is subaudible tone detection enabled?  If so, do decoding
		{
			//
			// Use Goertzel algorithm for subaudible tone detection
			//
			// We will detect differentially at three frequencies:  Above, below and on-frequency.  The two former will be used to provide a sample of the total energy
			// present as well as improve nearby-frequency discrimination.  By dividing the on-frequency energy with the averaged off-frequency energy we'll
			// get a ratio that is irrespective of the actual detected audio amplitude:  A ratio of 1.00 is considered "neutral" and it goes above unity with the increasing
			// likelihood that a tone was present on the target frequency
			//
			// Goertzel constants for the three decoders are pre-calculated in the function "UiCalcSubaudibleDetFreq()"
			//
			// (Yes, I know that below could be rewritten to be a bit more compact-looking, but it would not be much faster and it would be less-readable)
			//
			// Note that the "c" buffer contains audio that is somewhat low-pass filtered by the integrator, above
			//
			gcount++;// this counter is used for the accumulation of data over multiple cycles
			//
			for (uint16_t i = 0; i < blockSize; i++)
			{

				// Detect above target frequency
				AudioFilter_GoertzelInput(&ads.fm_goertzel[FM_HIGH],goertzel_buf[i]);
				// Detect energy below target frequency
				AudioFilter_GoertzelInput(&ads.fm_goertzel[FM_LOW],goertzel_buf[i]);
				// Detect on-frequency energy
				AudioFilter_GoertzelInput(&ads.fm_goertzel[FM_CTR],goertzel_buf[i]);
			}

			if (gcount >= FM_SUBAUDIBLE_GOERTZEL_WINDOW)// have we accumulated enough samples to do the final energy calculation?
			{
				s = AudioFilter_GoertzelEnergy(&ads.fm_goertzel[FM_HIGH]) + AudioFilter_GoertzelEnergy(&ads.fm_goertzel[FM_LOW]);
				// sum +/- energy levels:
				// s = "off frequency" energy reading

				r = AudioFilter_GoertzelEnergy(&ads.fm_goertzel[FM_CTR]);
				subdet = ((1 - FM_TONE_DETECT_ALPHA) * subdet)
						+ (r / (s / 2) * FM_TONE_DETECT_ALPHA);	// do IIR filtering of the ratio between on and off-frequency energy

				if (subdet > FM_SUBAUDIBLE_TONE_DET_THRESHOLD)// is subaudible tone detector ratio above threshold?
				{
					    tdet++;	// yes - increment count			// yes - bump debounce count
					if (tdet > FM_SUBAUDIBLE_DEBOUNCE_MAX)// is count above the maximum?
					{
						tdet = FM_SUBAUDIBLE_DEBOUNCE_MAX;// yes - limit the count
					}
				}
				else	 	// it is below the threshold - reduce the debounce
				{
					if (tdet)		// - but only if already nonzero!
					{
						tdet--;
					}
				}
				if (tdet >= FM_SUBAUDIBLE_TONE_DEBOUNCE_THRESHOLD)// are we above the debounce threshold?
				{
					ads.fm_subaudible_tone_detected = 1;// yes - a tone has been detected
				}
				else									// not above threshold
				{
					ads.fm_subaudible_tone_detected = 0;	// no tone detected
				}

				gcount = 0;		// reset accumulation counter
			}
		}
		else	 		// subaudible tone detection disabled
		{
			ads.fm_subaudible_tone_detected = 1;// always signal that a tone is being detected if detection is disabled to enable audio gate
		}
	}
}


//
//
//*----------------------------------------------------------------------------
//* Function Name       : audio_lms_notch_filter  [KA7OEI October, 2015]
//* Object              :
//* Object              : automatic notch filter
//* Input Parameters    : psize - size of buffer on which to operate
//* Output Parameters   :
//* Functions called    :
//*----------------------------------------------------------------------------
static void AudioDriver_NotchFilter(int16_t blockSize, float32_t *notchbuffer)
{
    static ulong		lms2_inbuf  = 0;
    static ulong		lms2_outbuf = 0;

    // DSP Automatic Notch Filter using LMS (Least Mean Squared) algorithm
    //
    arm_copy_f32(notchbuffer, &lmsData.lms2_nr_delay[lms2_inbuf], blockSize);	// put new data into the delay buffer
    //
    arm_lms_norm_f32(&lmsData.lms2Norm_instance, notchbuffer, &lmsData.lms2_nr_delay[lms2_outbuf], lmsData.errsig2, notchbuffer, blockSize);	// do automatic notch
    // Desired (notched) audio comes from the "error" term - "errsig2" is used to hold the discarded ("non-error") audio data
    //
    lms2_inbuf  += blockSize;				// update circular de-correlation delay buffer
    lms2_outbuf  = lms2_inbuf + blockSize;
    lms2_inbuf  %= ts.dsp_notch_delaybuf_len;
    lms2_outbuf %= ts.dsp_notch_delaybuf_len;
    //
}



//**********************************************************************************************************

static  void  AudioDriver_Mix ( float32_t* src, float32_t* dst, float32_t scaling, const uint16_t blockSize)
{
    float32_t                   e3_buffer[ 64 + 1 ];

    arm_scale_f32 ( src, scaling,   e3_buffer, blockSize);
    arm_add_f32   ( dst, e3_buffer, dst,       blockSize);
}


//***********************************************************************************************************


static  void AudioDriver_IQPhaseAdjust( uint16_t txrx_mode, float32_t* i_buffer, float32_t* q_buffer, const uint16_t blockSize )
{
    int16_t  trans_idx;

    // right now only in TX used, may change in future

    if ((txrx_mode == TRX_MODE_TX && ts.dmod_mode == DEMOD_CW) || ts.iq_freq_mode == FREQ_IQ_CONV_MODE_OFF) { trans_idx = IQ_TRANS_OFF; }
    else                                                                                                    { trans_idx = IQ_TRANS_ON;  }

    float32_t  iq_phase_balance =  (txrx_mode == TRX_MODE_RX)? ads.iq_phase_balance_rx: ads.iq_phase_balance_tx[trans_idx];

    if ( iq_phase_balance < 0 )   // we only need to deal with I and put a little bit of it into Q
    {
        AudioDriver_Mix( i_buffer, q_buffer, iq_phase_balance, blockSize );
    }
    else if ( iq_phase_balance > 0 )  // we only need to deal with Q and put a little bit of it into I
    {
        AudioDriver_Mix(  q_buffer,  i_buffer,  iq_phase_balance,  blockSize );
    }
}

//************************************************************************************************************

//**************************************************************************************************
/*												// ( a * b ) >> 30  le calcul en  64 bits
int32_t multiply_and_shift(int32_t a, int32_t b)
{
    int32_t  result;
    int64_t  temp_result;

    asm (
        "smull   %[temp_lo], %[temp_hi], %[operand_a], %[operand_b]\n"
        "mov     %[result], %[temp_hi], lsr #2\n"
        : [temp_lo] "=r" (temp_result), [temp_hi] "=r" (temp_result), [result] "=r" (result)
        : [operand_a] "r" (a), [operand_b] "r" (b)
        : "cc"
    );

    return result;
}
*/
//***************************************************************************************************

//************************************************************************************************************


void AudioDriver_SpectrumNoZoomProcessSamples ( float32_t * const i_sourc, float32_t * const q_sourc, const  uint16_t  blockSize )
{

    if ( sd.reading_ringbuffer == false && sd.fft_iq_len > 0 )
    {
 		for( int i = 0; i < blockSize; i++ )          // FFT buffers  filled with Q & I
		{
			sd.FFT_RingBuffer[sd.samp_ptr     ] = q_sourc[i];    // get floating point data for FFT for spectrum scope/waterfall display
			sd.FFT_RingBuffer[sd.samp_ptr + 1u] = i_sourc[i];
			sd.samp_ptr  += 2u;

			// On obtaining enough samples for spectrum scope/waterfall, update state machine, reset pointer and wait until we process what we have
			if (sd.samp_ptr >= sd.fft_iq_len-1)  { ts.FFT_sampling_enable = false;  sd.samp_ptr = 0; }  //*2)
		}
		sd.FFT_frequency = (ts.tune_freq); // spectrum shows all, LO is center frequency;
    }
}

//**********************************************************************************************************

//************************************************************************************************************

//**********************************************************************************************************

 void  AudioDriver_SpectrumZoomProcessSamples(  const  uint16_t  blockSize )   // magnify 2, 4, 8, 16, or 32
{
    if(sd.reading_ringbuffer == false && sd.fft_iq_len > 0)
    {

		// ZOOM FFT
		// is used here to have a very close look at a small part
		// of the spectrum display of the mcHF
		// The ZOOM FFT is based on the principles described in Lyons (2011)
		// 1. take the I & Q samples
		// 2. complex conversion to baseband (at this place has already been done in audio_rx_freq_conv!)
		// 3. lowpass I and lowpass Q separately (48kHz sample rate)
		// 4. decimate I and Q separately
		// 5. apply 256-point-FFT to decimated I&Q samples
		//
		// frequency resolution: spectrum bandwidth / 256
		// example: decimate by 8 --> 48kHz / 8 = 6kHz spectrum display bandwidth
		// frequency resolution of the display --> 6kHz / 256 = 23.44Hz
		// in 32x Mag-mode the resolution is 5.9Hz, not bad for such a small processor . . .
		//

		float32_t   x_buffer[ IQ_BLOCK_SIZE ];
		float32_t   y_buffer[ IQ_BLOCK_SIZE ];

		// lowpass Filtering
		// Mag 2x  - 12k   lowpass --> 24k  bandwidth
		// Mag 4x  -  6k   lowpass --> 12k  bandwidth
		// Mag 8x  -  3k   lowpass -->  6k  bandwidth
		// Mag 16x -  1k5  lowpass -->  3k  bandwidth
		// Mag 32x - 750Hz lowpass -->  1k5 bandwidth

		// 1st attempt - use low processor power biquad lowpass filters with 4 stages each
		arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_Zoom_FFT_I, adb.i_buffer, x_buffer, blockSize );
		arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_Zoom_FFT_Q, adb.q_buffer, y_buffer, blockSize) ;

		// arm_iir_lattice_f32_Nizar(&IIR_TXFilter, adb.i_buffer, adb.x_buffer, blockSize);
		// arm_iir_lattice_f32_Nizar(&IIR_TXFilter, adb.q_buffer, adb.y_buffer, blockSize);

		// decimation
		arm_fir_decimate_f32_Nizar ( &DECIMATE_ZOOM_FFT_I, x_buffer, x_buffer, blockSize );
		arm_fir_decimate_f32_Nizar ( &DECIMATE_ZOOM_FFT_Q, y_buffer, y_buffer, blockSize );
		// collect samples for spectrum display 256-point-FFT

		int16_t    blockSizeDecim = blockSize / ( 1 << sd.magnify );
		for(int16_t i = 0; i < blockSizeDecim; i++)
		{
			sd.FFT_RingBuffer[ sd.samp_ptr     ] = y_buffer[i];   // get floating point data for FFT for spectrum scope/waterfall display
			sd.FFT_RingBuffer[ sd.samp_ptr + 1u] = x_buffer[i];    //
			sd.samp_ptr += 2u;
			// On obtaining enough samples for spectrum scope/waterfall, update state machine, reset pointer and wait until we process what we have
			if( sd.samp_ptr >= sd.fft_iq_len - 1) {ts.FFT_sampling_enable = false;  sd.samp_ptr = 0; }     //*2)

		} // end for
		sd.FFT_frequency = (ts.tune_freq) + AudioDriver_GetTranslateFreq();  // spectrum shows center at translate frequency, LO + Translate Freq  is center frequency;


		// TODO: also insert sample collection for snap carrier here
		// and subsequently rebuild snap carrier to use a much smaller FFT (say 256 or 512)
		// in order to save many kilobytes of RAM ;-)


    }
}

#ifdef USE_FREEDV
//
// this is a experimental breakout of the audio_rx_processor for getting  FreeDV to working
// used to help us to figure out how to optimize performance. Lots of performance required for
// digital signal processing...
// then it will probably be most merged back into the rx_processor in  order to keep code duplication minimal

/*
 * @returns: true if digital signal should be used (no analog processing should be done), false -> analog processing maybe used
 * since no digital signal was detected.
 */
bool AudioDriver_RxProcessorDigital ( AudioSample_t * const src, float32_t * const dst, const uint16_t blockSize )
{
    bool retval = false;
    switch( ts.dmod_mode )
    {
		case DEMOD_LSB:
		case DEMOD_USB:
		case DEMOD_DIGI:
						retval = AudioDriver_RxProcessorFreeDV ( src, dst, blockSize );
						break;

		default:		arm_fill_f32 ( 0.0, dst, blockSize ); // this is silence
    }
    					// from here straight to final AUDIO OUT processing
    return retval;
}
#endif


static float32_t AudioDriver_FadeLeveler( int chan, float32_t audio, float32_t corr )
{
    assert ( chan < NUM_AUDIO_CHANNELS );

    static float32_t dc27     [NUM_AUDIO_CHANNELS]; // static will be initialized with 0
    static float32_t dc_insert[NUM_AUDIO_CHANNELS];

    dc27     [chan] = adb.mtauR * dc27     [chan] + adb.onem_mtauR * audio;
    dc_insert[chan] = adb.mtauI * dc_insert[chan] + adb.onem_mtauI * corr;
    audio           = audio     + dc_insert[chan] - dc27[chan];

    return audio;
}

//*----------------------------------------------------------------------------
//* Function Name       : SAM_demodulation [DD4WH, december 2016]
//* Object              : real synchronous AM demodulation with phase detector and PLL
//* Object              :
//* Input Parameters    : adb.i_buffer, adb.q_buffer
//* Output Parameters   : adb.a_buffer[0]
//* Functions called    :
//*----------------------------------------------------------------------------
static void AudioDriver_DemodSAM(int16_t blockSize)
{
    //#define STAGES    7

    // new synchronous AM PLL & PHASE detector
    // wdsp Warren Pratt, 2016
    //*****************************

    // First of all: decimation of I and Q path
    arm_fir_decimate_f32_Nizar( &FirDecim_RxSam_I,  adb.i_buffer,  adb.i_buffer, blockSize );      // LPF built into decimation (Yes, you can decimate-in-place!)
    arm_fir_decimate_f32_Nizar( &FirDecim_RxSam_Q,  adb.q_buffer,  adb.q_buffer, blockSize );      // LPF built into decimation (Yes, you can decimate-in-place!)

    switch(ts.dmod_mode)
    {
    	case DEMOD_AM:
					for(int i = 0; i < blockSize / adb.DF; i++)
					{
						float32_t audio;

						arm_sqrt_f32 (adb.i_buffer[i] * adb.i_buffer[i] + adb.q_buffer[i] * adb.q_buffer[i], &audio);
						if(ads.fade_leveler)
						{
							audio = AudioDriver_FadeLeveler(0,audio,0);
						}
						adb.a_buffer[0][i] = audio;
					}
					break;

		case DEMOD_SAM:
					{

						static uint16_t  count   = 0;

						static float32_t fil_out = 0.0;
						static float32_t lowpass = 0.0;
						static float32_t omega2  = 0.0;
						static float32_t phs     = 0.0;

						// Wheatley 2011 cuteSDR & Warren Pratts WDSP, 2016
						for(int i = 0; i < blockSize / adb.DF; i++)
						{   // NCO

							float32_t ai, bi, aq, bq;
							float32_t ai_ps, bi_ps, aq_ps, bq_ps;
							float32_t Sin, Cos;

							static float32_t dsI;             // delayed sample, I path
							static float32_t dsQ;             // delayed sample, Q path


							sincosf(phs,&Sin,&Cos);
							ai = Cos * adb.i_buffer[i];
							bi = Sin * adb.i_buffer[i];
							aq = Cos * adb.q_buffer[i];
							bq = Sin * adb.q_buffer[i];

							if (ads.sam_sideband != SAM_SIDEBAND_BOTH)
							{

#define OUT_IDX   (3 * SAM_PLL_HILBERT_STAGES)

								static float32_t a[3 * SAM_PLL_HILBERT_STAGES + 3];     // Filter a variables
								static float32_t b[3 * SAM_PLL_HILBERT_STAGES + 3];     // Filter b variables
								static float32_t c[3 * SAM_PLL_HILBERT_STAGES + 3];     // Filter c variables
								static float32_t d[3 * SAM_PLL_HILBERT_STAGES + 3];     // Filter d variables

								a[0] = dsI;
								b[0] = bi;
								c[0] = dsQ;
								d[0] = aq;
								dsI  = ai;
								dsQ  = bq;

								for (int j = 0; j < SAM_PLL_HILBERT_STAGES; j++)
								{
									int k = 3 * j;
									a[k + 3] = adb.c0[j] * (a[k] - a[k + 5]) + a[k + 2];
									b[k + 3] = adb.c1[j] * (b[k] - b[k + 5]) + b[k + 2];
									c[k + 3] = adb.c0[j] * (c[k] - c[k + 5]) + c[k + 2];
									d[k + 3] = adb.c1[j] * (d[k] - d[k + 5]) + d[k + 2];
								}

								ai_ps = a[OUT_IDX];
								bi_ps = b[OUT_IDX];
								bq_ps = c[OUT_IDX];
								aq_ps = d[OUT_IDX];

								// make room for next sample
								for (int j = OUT_IDX + 2; j > 0; j--)
								{
									a[j] = a[j - 1];
									b[j] = b[j - 1];
									c[j] = c[j - 1];
									d[j] = d[j - 1];
								}
							}

							float32_t audio[NUM_AUDIO_CHANNELS];

							float32_t corr[2] = { ai + bq, -bi + aq };

							switch(ads.sam_sideband)
							{
								case SAM_SIDEBAND_BOTH:	{	audio[0] = corr[0];								break;	}
								case SAM_SIDEBAND_USB:	{	audio[0] = (ai_ps - bi_ps) + (aq_ps + bq_ps);	break;	}
								case SAM_SIDEBAND_LSB:	{	audio[0] = (ai_ps + bi_ps) - (aq_ps - bq_ps);	break;	}
							}

							// "fade leveler", taken from Warren Pratts WDSP / HPSDR, 2016
							// http://svn.tapr.org/repos_sdr_hpsdr/trunk/W5WC/PowerSDR_HPSDR_mRX_PS/Source/wdsp/
							if(ads.fade_leveler)
							{
								for (int chan = 0; chan < NUM_AUDIO_CHANNELS; chan++)
								{
									audio[chan] = AudioDriver_FadeLeveler(chan, audio[chan], corr[0]);
								}
							}

				 /*
							a->dc = a->mtauR * a->dc + a->onem_mtauR * audio;
														a->dc_insert = a->mtauI * a->dc_insert + a->onem_mtauI * corr[0];
							audio += a->dc_insert - a->dc;
				   */

							for (int chan = 0; chan < NUM_AUDIO_CHANNELS; chan++)
							{
								adb.a_buffer[chan][i] = audio[chan];
							}

							// determine phase error
							float32_t phzerror = atan2f(corr[1], corr[0]);

							float32_t del_out = fil_out;

							omega2 = omega2 + adb.g2 * phzerror;									// correct frequency 1st step
							if      (omega2 < adb.omega_min)   { omega2 = adb.omega_min;     }
							else if (omega2 > adb.omega_max)   { omega2 = adb.omega_max;     }
							// correct frequency 2nd step
							fil_out = adb.g1 * phzerror + omega2;
							phs     = phs + del_out;

							// wrap round 2PI, modulus
							while (phs >= 2.0 * PI) phs -= (2.0 * PI);
							while (phs < 0.0) phs += (2.0 * PI);
						}
						   count++;
						if(count > 50) // to display the exact carrier frequency that the PLL is tuned to
							// in the small frequency display
							// we calculate carrier offset here and the display function is
							// then called in UiDriver_MainHandler approx. every 40-80ms
						{ // to make this smoother, a simple lowpass/exponential averager here . . .
							float32_t      carrier  =  0.1 * (omega2 * IQ_SAMPLE_RATE) / (adb.DF * 2.0 * PI);
										   carrier  =  carrier + 0.9 * lowpass;
							ads.carrier_freq_offset =  (int)carrier;
											  count = 0;
											lowpass = carrier;
						}
					}
					break;
    }
}


float32_t sign_new (float32_t x) { return x < 0 ? -1.0 : ( x > 0 ? 1.0 : 0.0); }


//***********************************************************************************************************************
void AudioDriver_Rx_Manual_IQ_Correction (float32_t * const  i_sour,
		                                  float32_t * const  q_sour,
										  const   uint16_t   paire,
										  const   uint16_t   blockSize )
{

	static uint16_t   IQ_auto_counter   = 0;
	static uint16_t   codec_restarts    = 0;
	static float32_t  phase_IQ_avg      = 0;
	const  float32_t  zzero             = 0.0;
	if (ts.switch_pause ) {  ts.switch_pause--; phase_IQ_avg = 0;  } /// pour ne pas ajuster les coeff de correction lors des switchs Tx/Rx
													 /// il suspend aussi tout ce qui est rafraichissement d'affichage sur ecran ( spectre , voltage , temperature  et S-meter)
													 /// il suspend aussi la mise a jour du gain-auto entr�e lines du Codec WM8731
													 /// il suspend aussi certains calcul non prioritaires ( Noise Blanker , S-meter compute ... )
													 /// Ceci surtout pour lib�rer les ports I2S exclusivement � l'ecran tactile touchescreen
													 /// tout ceci en vue de le fiabiliser, tout en ne pas mettre a jour les Coeff IQ lors des transition Tx/Rx


			 //**************** Automatic IQ imbalance correction******************
				 // Moseley, N.A. & C.H. Slump (2006): A low-complexity feed-forward I/Q imbalance compensation algorithm.
				 // in 17th Annual Workshop on Circuits, Nov. 2006, pp. 158-164.
				 // http://doc.utwente.nl/66726/1/moseley.pdf
				 ///_______________________________________________________________________

	if ((ts.switch_pause == 0)  && ((paire & 0x03)== 0x02) )  // = 2..2
	{
				float32_t     correla_iq     = zzero;
				float32_t     absolute_i     = zzero;
				float32_t     absolute_q     = zzero;

		static	float32_t     correla_iq_avg = 0;
		static 	float32_t     absolute_i_avg = 0;
		static	float32_t     absolute_q_avg = 0;

		for ( uint32_t i = 0; i < blockSize; i++ )
		{
			if      ( i_sour[i] > zzero) { correla_iq -= q_sour[i];  absolute_i += i_sour[i]; }
			else if ( i_sour[i] < zzero) { correla_iq += q_sour[i];  absolute_i -= i_sour[i]; }

			if      ( q_sour[i] > zzero) { absolute_q += q_sour[i];  }
			else if ( q_sour[i] < zzero) { absolute_q -= q_sour[i];  }
		}


		///   Ajustement des parametres de corrections I/Q apres  64 echantillons
		///

		int   devidend  = 1024;  // if ( ts.iq_auto_correction ) devidend = 512;
		correla_iq_avg +=  correla_iq  -  correla_iq_avg / devidend ;    /// eq (34) and first order lowpass
		absolute_i_avg +=  absolute_i  -  absolute_i_avg / devidend ;    /// eq (35) and first order lowpass
		absolute_q_avg +=  absolute_q  -  absolute_q_avg / devidend ;    /// eq (36) and first order lowpass

		// N96K P0-3: abs() -> fabsf() for float32_t thresholds (audit A3)
		if(fabsf(absolute_i_avg) > 0.0001) {  adb.phase_corr = correla_iq_avg / absolute_i_avg; }  /// calcul coeff correctif de dephasage I/Q : M_c1
		else                             {  adb.phase_corr = zzero;                           }  /// prevent divide-by-zero// eq (30)

		float32_t                 help     = ( absolute_i_avg * absolute_i_avg );  /// au carr�
		if  (help > 0.0001)    {  help     = ( absolute_q_avg * absolute_q_avg - correla_iq_avg * correla_iq_avg ) / help; } /// prevent divide-by-zero// eq (31)

		if  (help > 0.0001)    {  adb.ampli_corr = sqrtf(help); } /// Calcul coeff correctif d'amplitude I/Q :M_c2
		else                   {  adb.ampli_corr = 1.0;         } /// prevent sqrtf of negative value // eq (31)

										// Test and fix of the "twinpeak syndrome"
										// which occurs sporadically and can -to our knowledge- only be fixed
										// by a reset of the codec
										// It can be identified by a totally non-existing mirror rejection,
										// so I & Q have essentially the same phase
										// We use this to identify the snydrome and reset the codec accordingly:
										// calculate phase between I & Q

										IQ_auto_counter++;

										if (IQ_auto_counter >= 6500u)  /// chaque 4.66 secondes
										{


											// N96K P0-3: abs() -> fabsf() for float32_t threshold (audit A3)
											if (fabsf(absolute_q_avg) > 0.00001)  // si aprÃ¨s delay du dernier demarrage et no divide-by-zero
											{
															// twinpeak_tested = 2 --> wait for system to warm up
															// twinpeak_tested = 0 --> go and test the IQ phase
															// twinpeak_tested = 1 --> tested, verified, go and have a nice day!
															// Moseley & Slump (2006) eq. (33)
															// this gives us the phase error between I & Q in radians


												float32_t  phase_IQ      = correla_iq_avg / absolute_q_avg;  /// plus simple
														   phase_IQ_avg += (-phase_IQ_avg + phase_IQ)/4;

												if (( phase_IQ_avg > 0.353 || phase_IQ_avg < -0.353 ) && codec_restarts < 100u )  /// si dephasage > +/- 22Â° redemarrage carte son codec (prÃ©venir des dÃ©verouillages sporadiques de la carte son )
												{
															// threshold of 22.5 degrees phase shift == PI / 8 == 0.3926990817
															// hopefully your hardware is not so bad, that its phase error is more than 22 degrees ;-)
															// if it is that bad, adjust this threshold to maybe PI / 7 or PI / 6

													  Codec_RestartI2S();  /// redemarrage de la liaison serie I2S3 avec la carte son (le codec)
													  codec_restarts++;
													  correla_iq_avg        = 0.0;   /// remise a zero des parametres de correction IQ aprÃ¨s redÃ©marrage de la carte son codec
													  absolute_i_avg        = 0.0;
													  absolute_q_avg        = 0.0;
													  phase_IQ_avg          = 0.0;
													  ts.switch_pause      += 2500;  // pour temporiser juste après Codec Restart
															// TODO: we should print out a message, if twinpeaks remains after the
															// 5th reset for example --> could then be a severe hardware error !
															// if(codec_restarts > 239) {  printf("hardware Error !!! pse report to developper" ); } /// PRINT OUT WARNING MESSAGE    /// 4
												}
											}
											IQ_auto_counter =   0;
										}
	}


	arm_scale_f32             ( i_sour, ts.rx_adj_gain_var.i,  i_sour,  blockSize); // Apply  Manual I/Q amplitude imbalance corrections
 // arm_scale_f32             ( q_sour, ts.rx_adj_gain_var.q,  q_sour,  blockSize); // TODO: we need only scale one channel! DD4WH, Dec 2016
 //	AudioDriver_IQPhaseAdjust ( ts.txrx_mode, i_sour        ,  q_sour,  blockSize); // Apply Manual I/Q phase correction
	float32_t   phase_correction = ads.iq_phase_balance_rx;
	for ( uint32_t i = 0; i<blockSize; i++) { q_sour[i] += phase_correction * i_sour[i]; }

}

//***********************************************************************************************************************

void AudioDriver_Rx_Auto_IQ_Correction (float32_t * const i_sour, float32_t * const q_sour, const uint16_t paire, const  uint16_t  blockSize )
{
    static uint16_t   IQ_auto_counter   = 0;
    static uint16_t   codec_restarts    = 0;
    static float32_t  phase_IQ_avg      = 0;
    const  float32_t  zzero             = 0.0;
    if (ts.switch_pause ) {  ts.switch_pause--; phase_IQ_avg = 0;  } /// pour ne pas ajuster les coeff de correction lors des switchs Tx/Rx
                                                     /// il suspend aussi tout ce qui est rafraichissement d'affichage sur ecran ( spectre , voltage , temperature  et S-meter)
                                                     /// il suspend aussi la mise a jour du gain-auto entr�e lines du Codec WM8731
                                                     /// il suspend aussi certains calcul non prioritaires ( Noise Blanker , S-meter compute ... )
                                                     /// Ceci surtout pour lib�rer les ports I2S exclusivement � l'ecran tactile touchescreen
                                                     /// tout ceci en vue de le fiabiliser, tout en ne pas mettre a jour les Coeff IQ lors des transition Tx/Rx


     		 //**************** Automatic IQ imbalance correction******************
	    		 // Moseley, N.A. & C.H. Slump (2006): A low-complexity feed-forward I/Q imbalance compensation algorithm.
				 // in 17th Annual Workshop on Circuits, Nov. 2006, pp. 158-164.
				 // http://doc.utwente.nl/66726/1/moseley.pdf
    			 ///_______________________________________________________________________

	if ((ts.switch_pause == 0)  && ((paire & 0x03)== 0x02) )  // = 2..2
	{
				float32_t     correla_iq     = zzero;
				float32_t     absolute_i     = zzero;
				float32_t     absolute_q     = zzero;

		static	float32_t     correla_iq_avg = 0;
		static 	float32_t     absolute_i_avg = 0;
		static	float32_t     absolute_q_avg = 0;

 	 	for ( uint32_t i = 0; i < blockSize; i++ )
        {
 	 		if      ( i_sour[i] > zzero) { correla_iq -= q_sour[i];  absolute_i += i_sour[i]; }
 	 		else if ( i_sour[i] < zzero) { correla_iq += q_sour[i];  absolute_i -= i_sour[i]; }

 	 		if      ( q_sour[i] > zzero) { absolute_q += q_sour[i];  }
 	 		else if ( q_sour[i] < zzero) { absolute_q -= q_sour[i];  }
        }


	    ///   Ajustement des parametres de corrections I/Q apres  64 echantillons
		///

        //int devidend  = 1024;  if ( ts.iq_auto_correction ) devidend = 512;

 	 	int devidend    = 512;
        correla_iq_avg +=  correla_iq  -  correla_iq_avg / devidend ;    /// eq (34) and first order lowpass
        absolute_i_avg +=  absolute_i  -  absolute_i_avg / devidend ;    /// eq (35) and first order lowpass
        absolute_q_avg +=  absolute_q  -  absolute_q_avg / devidend ;    /// eq (36) and first order lowpass

		// N96K P0-3: abs() -> fabsf() for float32_t thresholds (audit A3)
		if(fabsf(absolute_i_avg) > 0.0001) {  adb.phase_corr = correla_iq_avg / absolute_i_avg; }  /// calcul coeff correctif de dephasage I/Q : M_c1
		else                             {  adb.phase_corr = zzero;                           }  /// prevent divide-by-zero// eq (30)

		float32_t                 help     = ( absolute_i_avg * absolute_i_avg );  /// au carr�
		if  (help > 0.0001)    {  help     = ( absolute_q_avg * absolute_q_avg - correla_iq_avg * correla_iq_avg ) / help; } /// prevent divide-by-zero// eq (31)

		if  (help > 0.0001)    {  adb.ampli_corr = sqrtf(help); } /// Calcul coeff correctif d'amplitude I/Q :M_c2
		else                   {  adb.ampli_corr = 1.0;         } /// prevent sqrtf of negative value // eq (31)

										// Test and fix of the "twinpeak syndrome"
										// which occurs sporadically and can -to our knowledge- only be fixed
										// by a reset of the codec
										// It can be identified by a totally non-existing mirror rejection,
										// so I & Q have essentially the same phase
										// We use this to identify the snydrome and reset the codec accordingly:
										// calculate phase between I & Q

										IQ_auto_counter++;

										if (IQ_auto_counter >= 6500u)  /// chaque 4.66 secondes
										{


											// N96K P0-3: abs() -> fabsf() for float32_t threshold (audit A3)
											if (fabsf(absolute_q_avg) > 0.00001)  // si aprÃ¨s delay du dernier demarrage et no divide-by-zero
											{
															// twinpeak_tested = 2 --> wait for system to warm up
															// twinpeak_tested = 0 --> go and test the IQ phase
															// twinpeak_tested = 1 --> tested, verified, go and have a nice day!
															// Moseley & Slump (2006) eq. (33)
															// this gives us the phase error between I & Q in radians


												float32_t  phase_IQ      = correla_iq_avg / absolute_q_avg;  /// plus simple
														   phase_IQ_avg += (-phase_IQ_avg + phase_IQ)/4;

												if (( phase_IQ_avg > 0.353 || phase_IQ_avg < -0.353 ) && codec_restarts < 100u )  /// si dephasage > +/- 22Â° redemarrage carte son codec (prÃ©venir des dÃ©verouillages sporadiques de la carte son )
												{
															// threshold of 22.5 degrees phase shift == PI / 8 == 0.3926990817
															// hopefully your hardware is not so bad, that its phase error is more than 22 degrees ;-)
															// if it is that bad, adjust this threshold to maybe PI / 7 or PI / 6

													  Codec_RestartI2S();  /// redemarrage de la liaison serie I2S3 avec la carte son (le codec)
													  codec_restarts++;
													  correla_iq_avg        = 0.0;   /// remise a zero des parametres de correction IQ aprÃ¨s redÃ©marrage de la carte son codec
													  absolute_i_avg        = 0.0;
													  absolute_q_avg        = 0.0;
													  phase_IQ_avg          = 0.0;
													  ts.switch_pause      += 2500;  // pour temporiser juste après Codec Restart
															// TODO: we should print out a message, if twinpeaks remains after the
															// 5th reset for example --> could then be a severe hardware error !
															// if(codec_restarts > 239) {  printf("hardware Error !!! pse report to developper" ); } /// PRINT OUT WARNING MESSAGE    /// 4
												}
											}
											IQ_auto_counter =   0;
										}
	}


//	if ( ts.iq_auto_correction ) //Apply  Manual I/Q imbalances corrections
	{
		// first correct Q and then correct I --> this order is crucially important!
		for ( uint32_t i = 0; i<blockSize; i++) { q_sour[i] += adb.phase_corr * i_sour[i]; }  // Phase Correction en phase  see fig. 5
		arm_scale_f32 ( i_sour     , adb.ampli_corr,  i_sour,  blockSize );  // Amplitude Correction en amplitude
	}


}
///*******************************************************************************************
//***********************************************************************************************************************

///*******************************************************************************************


static void  AudioDriver_RxProcessorNoiseReduction ( uint16_t blockSizeDecim,  float32_t*  inout_buffer )  // static ajout�e
{
#ifdef   USE_ALTERNATE_NR

  //const uint8_t      dsp_active       = ts.dsp_active;
    static int         trans_count_in   = 0;
    static int         outbuff_count    = 0;
    static int         NR_fill_in_pt    = 0;
    static NR_Buffer*  out_buffer       = NULL;
    static float32_t   NR_dec_buffer[NR_DEC_BUFFER_SIZE];   // N96K B3: fixed ISR buffer (was a VLA)

#ifdef USE_FREEDV
    // N96K A2 (upstream 27945d8b): NR and FreeDV share the same MultiModeBuffer union (mmb). While
    // FreeDV is active this function must not touch the shared buffers: leave the audio untouched
    // (pass-through) and keep the NR state for the next analog session.
    if ( ts.dmod_mode == DEMOD_DIGI && ts.digital_mode == DigitalMode_FreeDV )
    {
        return;
    }
#endif

    // N96K B3: coherent NR pipeline restart (audit R9/A4). AudioNr_Prepare() is called in UI context
    // and only raises the request; drop the partially filled input block and both FIFO queues here,
    // in the context which owns them, and pass this one block through untouched (it is still 12 kHz
    // audio, exactly like the normal output of this function after its decimate/interpolate step).
    if (NR_restart_request)
    {
        trans_count_in = 0;
        outbuff_count  = 0;
        NR_fill_in_pt  = 0;
        out_buffer     = NULL;
        AudioNr_IsrRestart();
        return;
    }

    // this would be the right place for another decimation-by-2 to get down to 6ksps
    // in order to further improve the spectral noise reduction
    // TEST!
    //
    // anti-alias-filtering is already provided at this stage by the IIR main filter
    // Only allow another decimation-by-two, if filter bandwidth is <= 2k7
    //
    // Add decimation-by-two HERE

    //  decide whether filter < 2k9!!!

    uint32_t    no_dec_samples = blockSizeDecim; // only used for the noise reduction decimation-by-two handling

    // N96K B3: all current callers use blockSizeDecim == 8 (BS_8); never let a future caller overrun
    // the fixed ISR buffer - pass the block through instead of writing out of bounds.
    if (blockSizeDecim > NR_DEC_BUFFER_SIZE)
    {
        return;
    }

    // buffer needs max blockSizeDecim , less if second decimation is done
    // see below

    bool doSecondDecimation = (FilterInfo[FilterPathInfo[ts.filter_path].id].width < 2901); //  || (ts.txrx_mode == TRX_MODE_TX);

    ts.doSecondDecimation   = doSecondDecimation;

    if ( doSecondDecimation == true )
    {
           no_dec_samples = (blockSizeDecim >> 1u);  // 4 echantillons en 6000 Khz / 32 en 48000 Khz

           //if ( !ts.show_debug_info )
           {
				//  decimate-by-2, DECIMATE_NR, in place
				  arm_fir_decimate_f32_Nizar ( &DECIMATE_NR, inout_buffer, inout_buffer, blockSizeDecim );
           }
      /*     else
           {
         				// decimation par 2 sans filtration suplementaire, reponse plate et moindre calcul
        	    for ( int i = 1; i < 4u; i++ ) { inout_buffer[i] = inout_buffer[ i << 1u ]; }
           }*/

    }
    else
    {
    	arm_fir_f32_Nizar ( &Fir_NR_PRES_FILTRAGE, inout_buffer, inout_buffer, blockSizeDecim );
    }


    // attention -> change loop no into no_dec_samples!

    for ( int k = 0; k < no_dec_samples; k +=2u ) //transfer our noisy audio to our NR-input buffer
    {
        mmb.nr_audio_buff [ NR_fill_in_pt ].samples [ trans_count_in ].real = inout_buffer [ k      ];
        mmb.nr_audio_buff [ NR_fill_in_pt ].samples [ trans_count_in ].imag = inout_buffer [ k + 1u ];

        trans_count_in++; // count the samples towards FFT-size  -  2 samples per loop
    }

    if ( trans_count_in  >= ( NR_FFT_SIZE_128 >> 1u ) )
        //NR_FFT_SIZE_128 has to be an integer mult. of blockSizeDecim!!!
    {
        if (NR_in_buffer_add ( &mmb.nr_audio_buff[ NR_fill_in_pt ] ) == 0)
        {
            N96K_DIAG_INC(audio_diag.nr_in_full);   // N96K B3: NR FFT fell behind, input FIFO is full
        }
        trans_count_in = 0;                                		  // set counter to 0
        NR_fill_in_pt++;                                   		  // increase pointer index
        NR_fill_in_pt  &= 0x3;   // NR_fill_in_pt  %=  NR_BUFFER_NUM;  // make sure, that index stays in range

        //at this point we have transfered one complete block of 128 (?) samples to one buffer
    }

    //**********************************************************************************
    //don't worry!  in the mean time the noise reduction routine is (hopefully) doing it's job within ui
    //as soon as "fdv_audio_has_data" we can start harvesting the output
    //**********************************************************************************

    if (out_buffer == NULL && NR_out_has_data() > 1)
    {
        NR_out_buffer_peek( &out_buffer );
    }

    // N96K P0-3: NR_dec_buffer used to stay uninitialised when no NR output block was available
    // (NR just switched on or the UI thread fell behind); it is read unconditionally below and
    // copied into the RX audio path, so interrupt stack garbage reached the codec. Silence costs
    // only 4..8 float stores in the ISR, the out_buffer != NULL branch overwrites all of them.
    arm_fill_f32 ( 0, NR_dec_buffer, no_dec_samples ); // N96K P0-3: init NR buffer (audit A1)

    if (out_buffer != NULL)  //NR-routine has finished it's job
    {
        for (int j=0; j < no_dec_samples; j += 2u) // transfer noise reduced data back to our buffer
        {
            NR_dec_buffer[j   ] = out_buffer->samples[outbuff_count+NR_FFT_SIZE_128].real; //here add the offset in the buffer
            NR_dec_buffer[j+1u] = out_buffer->samples[outbuff_count+NR_FFT_SIZE_128].imag; //here add the offset in the buffer
            outbuff_count++;
        }

        if (outbuff_count >= (NR_FFT_SIZE_128 >> 1u)) // we reached the end of the buffer coming from NR
        {
            outbuff_count = 0;          NR_out_buffer_remove( &out_buffer );
            out_buffer    = NULL;       NR_out_buffer_peek  ( &out_buffer );
        }
    }
    else
    {
        N96K_DIAG_INC(audio_diag.nr_out_empty);   // N96K B3: no NR output ready -> silence inserted
    }


    // interpolation of a_buffer from 6ksps to 12ksps!
    // from NR_dec_buffer --> a_buffer
    // but only, if we have decimated to 6ksps, otherwise just copy the samples into a_buffer

    if ( doSecondDecimation == true )
    {
       // arm_fir_interpolate_f32_Nizar ( &INTERPOLATE_NR, NR_dec_buffer, inout_buffer, no_dec_samples );
       // arm_scale_f32           ( inout_buffer   , 2.0          , inout_buffer, blockSizeDecim );


       //*********************************************************************************************************
       //******** Interpolation 6Khz-->12Khz avec filtre IIR 8em ordre cette fois ci aulieu du FIR 40 taps *******
       //******** cela devrait étre un peu plus rapide et un peu plus ajusté en bande 2.86Khz a -3db *************
       //******** un peu plus rapide et avec une distorsion de propagation de phase raisonnable < 1 ms ***********
       //************* Nizar  15 Avril 2023
       //**********************************************************************************************************

	   inout_buffer[0] = NR_dec_buffer[0];
	   inout_buffer[2] = NR_dec_buffer[1];
	   inout_buffer[4] = NR_dec_buffer[2];
	   inout_buffer[6] = NR_dec_buffer[3];
	   inout_buffer[1] = NR_dec_buffer[0];
	   inout_buffer[3] = NR_dec_buffer[1];
	   inout_buffer[5] = NR_dec_buffer[2];
	   inout_buffer[7] = NR_dec_buffer[3];

	   arm_iir_lattice_f32_Nizar ( &IIR_NR_Interpolation, inout_buffer, inout_buffer, blockSizeDecim );

    }
    else
    {
        arm_copy_f32 ( NR_dec_buffer, inout_buffer, blockSizeDecim );
    }
#endif
}


//*******************************************************************************************************
//*************************************************************************************************************************

// interpolation avec la fonction ( Xn + Xn-1 ) / 2   au milieu de deux points

void   arm_scale_Mid_interpolate_f32_Nizar( float32_t * pSrc,  float32_t scale,  float32_t * pDst,  uint32_t blockSize )
{
            uint32_t    blkCnt;                          // loop counter
  static    float32_t   last_in1 = 0;
            float32_t   in1,  in2, in3, in4;             // temporary variabels
            float32_t   scale_half = scale * 0.5;
  blkCnt = (blockSize >> 2u);

  while ( blkCnt > 0u )
  {
	in1 = *pSrc;
    in2 = *(pSrc + 1);
    in1 = in1 * scale;		// multiply with scaling factor
    in3 = *(pSrc + 2);      // read input sample from source
    in2 = in2 * scale; 		// multiply with scaling factor

    in4 = *(pSrc + 3);		// read input sample from source

    in3 = in3 * scale;		// multiply with scaling factor


    *pDst       =  ( in1  + last_in1 ) * 0.5;		// store the result to destination
    *(pDst + 1) =    in1;
     in4        =    in4 * scale;
    *(pDst + 2) =  ( in2 + in1 ) * 0.5;
    *(pDst + 3) =    in2 ;
    *(pDst + 4) =  ( in3 + in2 ) * 0.5;
    *(pDst + 5) =    in3;
    *(pDst + 6) =  ( in4 + in3 ) * 0.5;
    *(pDst + 7) =    in4;
    last_in1    =    in4;
    pSrc       +=    4u;	 // update pointers to process next samples
    pDst       +=    8u;
    blkCnt--;				 // Decrement the loop counter
  }
}



//**********************************************************************************************************
//**********************************************************************************************************

// simple decimation par echantiollnnage moitié  sans filtrage ( il est deja filtre)

void   arm_direct_half_decimation_f32_Nizar ( float32_t * pSrc,  float32_t * pDst,  uint32_t blockSize )
{
  uint32_t   blkCnt = (blockSize >> 2u);  // division par quatre

  while ( blkCnt > 0u )
  {
    *pDst     =  *pSrc;
	*(pDst+1) =  *(pSrc + 2);
	*(pDst+2) =  *(pSrc + 4);
    *(pDst+3) =  *(pSrc + 6);
     pSrc    +=   8u;
     pDst    +=   4;
     blkCnt--;
  }
}

//**********************************************************************************************************
//
//*----------------------------------------------------------------------------
//* Function Name       : audio_rx_processor
//* Object              :
//* Object              : audio sample processor
//* Input Parameters    :
//* Output Parameters   :
//* Functions called    :
//*----------------------------------------------------------------------------
//***************************************************************************************************************
//***************************************************************************************************************
//***************************************************************************************************************

static  void  AudioDriver_RxProcessor( AudioSample_t * const src_Codec,  AudioSample_t * const dst_Codec )
{
    // this is the main RX audio function
	// it is driven with 64 samples in the complex buffer scr, meaning 64 X ( I & Q)
    // we copy volatile variables which are used multiple times to local consts to let the compiler to its optimization magic
    // since we are in an interrupt, no one will change these anyway
    // shaved off a few bytes of code

    const  uint8_t   dmod_mode       = ts.dmod_mode;
    const  uint8_t   tx_audio_source = ts.tx_audio_source;
    const  uint8_t   iq_freq_mode    = ts.iq_freq_mode;
    const  uint8_t   dsp_active      = ts.dsp_active;
    static uint16_t  pass_count      = 0u;

    static  	  float32_t     ii_buffer [64];
    static  	  float32_t     qq_buffer [64];

    int16_t     *Sample_16 = (int16_t*) src_Codec;

   uint8_t       BS_8  =  8;
   uint16_t      BS_32 = 32;
   uint16_t      BS_64 = 64;

	static  uint8_t   dsp_delay_ok = 0;

	                //*************************************************************************************************************
					//************************** Rx   Mute boolean    *************************************************************
					//*************************************************************************************************************

    bool  mute_output = ts.audio_dac_output_muting_flag || (ts.audio_dac_output_muting_buffer_count > 0) || (ads.af_disabled)
#ifdef TUNISIANN
    	//	|| RadioManagement_IsTxDisabled()
#endif
			;

       //  if (dmod_mode == DEMOD_FM)  {  mute_output |= ads.fm_squelched; }   // squlech pour FM

    			  //************************************************************************************************************
          	  	  //************************************ Rx  Mute Audio output *************************************************
          	  	  //************************************************************************************************************
            	  // fill audio buffers with zeroes if we are to mute the receiver completely while still processing data OR it is in FM and squelched
            	  // or when filters are switched

	if ( mute_output )    /// FM squelsh or filters switch, on passe directement à une sortie nulle sans aucun traitement
	{
		arm_fill_f32 ( 0, (float32_t*) dst_Codec, 128 );  // destination to Speaker valeur nulle

		if ( ts.audio_dac_output_muting_buffer_count ) {  ts.audio_dac_output_muting_buffer_count--;  }
	}

 	else        //  if  not  mute_output
	{
			 //************************************************************************************
		     //**************************** Rx  : to USB Cable & PC for mode I/Q ******************
		     //************************************************************************************

		if ( tx_audio_source == TX_AUDIO_DIG_IQ )  // sortie directe vers Cable USB du PC pour mode I/Q ,integralement sans aucune modification signal
		{
			if ( ts.Audio_Card_mchf_playing )
			{
				for ( uint32_t i = 0; i < 256 ; i += 4u )
				{
					audio_USB_1_output_buffer (  Sample_16[ i     ] );  // we collect our I/Q samples for USB transmission if TX_AUDIO_DIG_IQ
					audio_USB_1_output_buffer (  Sample_16[ i + 2u] );
				}
			}
		}

			 //*************************************************************************************
			 //***********  Rx : Verifications thresholds signal entrée sur 16 bits seulement*******
			 //*************************************************************************************


		pass_count++;    // compteur pour sauter des trames lors de calcul des depassement des threshold entree signal , allege le calcul

	    if ( ts.switch_pause == 0u)        // pour suspendre cette tache occupante non prioritaire en cas de besoin , lors des touches tactiles
		{
			if ( !(pass_count & 0x03u) )   // ..0...0..  une fois sur quatre
			{
				uint16_t    thrshd_4 = ADC_CLIP_WARN_THRESHOLD_4;
				uint16_t    thrshd_2 = ADC_CLIP_WARN_THRESHOLD_2;
				uint16_t    thrshd_1 = ADC_CLIP_WARN_THRESHOLD_1;

				for ( uint32_t i = 0; i < 128; i+=2 )
				{
					uint16_t  val = abs ( Sample_16 [i] );

					if ( val >  thrshd_4 )           // This is the release threshold for the auto RF gain
					{
						ads.adc_quarter_clip = 1u;
						if ( val > thrshd_2 )        // This is the trigger threshold for the auto RF gain
						{
							ads.adc_half_clip++;     //  = 1;
							if ( val > thrshd_1 )    // This is the threshold for the red clip indicator on S-meter
							{
								ads.adc_clip++;
							}
						}
					}
				}
			}
		}


			//******************************************************************************************************************************
			//************************************* RX Noise Blanker large bande 96Khz (Nouvelle Function) *********************************
			//******************************************************************************************************************************

	    if ( dmod_mode != DEMOD_CW )
	    {
			if ( ts.nb_setting )  AudioDriver_NoiseBlanker_int ( src_Codec );  // do Wide Band Noise-Blanker function ( old Function )
	    }

      	//*********************************************************************************************************************************
      	//************************************  Rx  Conversion Scr integers to buffer Floats *************************************************************
      	//*********************************************************************************************************************************


		for ( uint32_t i = 0;  i < BS_64;  i++   )     // 32 bit format - convert to float and increment
		{
			ii_buffer [ i ] = (float32_t)  I2S_correctHalfWord ( src_Codec[ i ].l );
			qq_buffer [ i ] = (float32_t)  I2S_correctHalfWord ( src_Codec[ i ].r );
		}

    	//*********************************************************************************************************************************
    	//********************************* Rx Scale Down Buffers to old 16 bits values ***************************************************
    	//*********************************************************************************************************************************

		arm_scale_f32 ( ii_buffer,  0.1525e-4,  ii_buffer,  BS_64 );
    	arm_scale_f32 ( qq_buffer,  0.1525e-4,  qq_buffer,  BS_64 );


       	//***************************************************************************************************
		//*************************** Rx Manual Correction  I-Q  96Khz ********************************************
		//***************************************************************************************************

    	if (! ts.iq_auto_correction )
    	{
    		AudioDriver_Rx_Manual_IQ_Correction ( ii_buffer, qq_buffer, pass_count, BS_64 );
    	}

        //***************************************************************************************************
        //*************************** Rx Spectrum Normal Process *********************************************
        //***************************************************************************************************


    	if ( (sd.magnify == 0u) && ts.FFT_sampling_enable )

    	AudioDriver_SpectrumNoZoomProcessSamples ( ii_buffer, qq_buffer, BS_64 ); // Spectrum display sample collect for magnify == 0


    	//****************************************************************************************************
    	//************************ Rx  Filtrage LPF IIR 4em  16.5Khz  avant decimation 96Khz--> 48Khz ********
    	//****************************************************************************************************

    	arm_iir_lattice_f32_Nizar ( &IIR_Rx_LPF_16K_Filter_I,  ii_buffer,  ii_buffer,  BS_64  );  // Nizar
    	arm_iir_lattice_f32_Nizar ( &IIR_Rx_LPF_16K_Filter_Q,  qq_buffer,  qq_buffer,  BS_64  );  // Nizar


    	//***************************************************************************************************
		//*************************** Rx  Automatic Correction  I-Q  96Khz **********************************
		//***************************************************************************************************

    	if ( ts.iq_auto_correction )
    	{
    		AudioDriver_Rx_Auto_IQ_Correction( ii_buffer, qq_buffer, pass_count,  BS_64 );
    	}

    	//****************************************************************************************************
    	//***************************   Rx Decimation 1/2  96Khz--> 48Khz    *********************************
    	//****************************************************************************************************

    /*	for (  uint32_t  io = 0 ;  io < BS_32;  io++  )		// on va prendre un point sur deux
    	{
    		adb.i_buffer[ io  ] =  ii_buffer[  io << 1u ];
    		adb.q_buffer[ io  ] =  qq_buffer[  io << 1u ];
    	}*/

    	arm_direct_half_decimation_f32_Nizar( ii_buffer,  adb.i_buffer,  BS_32 );  // On prend 1 pt sur deux
    	arm_direct_half_decimation_f32_Nizar( qq_buffer,  adb.q_buffer,  BS_32 );

        //**************************************************************************************************
        //******************* Rx  Demodulation de frequence Xlat  +/-12Khz , +/-6Khz  (FS=48Khz)  **********
        //**************************************************************************************************

        if ( iq_freq_mode )            // is receive frequency conversion to be done?
        {
            AudioDriver_FreqConversion ( adb.i_buffer, adb.q_buffer, BS_32, iq_freq_mode == FREQ_IQ_CONV_P6KHZ || iq_freq_mode == FREQ_IQ_CONV_P12KHZ );
        }


        //**************************************************************************************************
        //*************************** Rx Spectre Zoom  *******************************************************
        //**************************************************************************************************

         if ((sd.magnify != 0) && ts.FFT_sampling_enable ) AudioDriver_SpectrumZoomProcessSamples ( BS_32 );   // Spectrum display sample collect for magnify != 0


        ///*************************************************************************************************
        //***************************** Rx FREE DV *********************************************************
        //**************************************************************************************************



#ifdef USE_FREEDV
        bool  dvmode_signal = false;

        if ( ts.dvmode == true  &&  ts.digital_mode == DigitalMode_FreeDV )
        {
            dvmode_signal = AudioDriver_RxProcessorDigital ( src_Codec , adb.a_buffer[1], BS_32 );
        }

        if ( dvmode_signal == false ) // If Not  FREEDV
#endif

        //*************************************************************************************************
        //****************************** Rx Traitement des Modes USB , LSB , CW , AM , SAM ,DIGITAL *******
        //*************************************************************************************************

        {
  							// A exclure Les modes FM, AM et SAM  qui feront leur propre decimation par eux même

            if (( dmod_mode != DEMOD_FM ) && ( dmod_mode != DEMOD_SAM) && ( dmod_mode != DEMOD_AM )) // USB, LSB, CW , Digital
            {

            				//*************************************************************************************************
            	        	//***** Rx  LPF 3.6Khz ( FIR 43taps) + Decimation 48Khz --> 12Khz ,  USB , LSB , CW , Digital   ***
            	        	//*************************************************************************************************

            	 arm_fir_decimate_f32_Nizar ( &DECIMATE_RX_I, adb.i_buffer, adb.i_buffer, BS_32 );  // Nizar    // LPF 3.6Khz ( FIR 43taps) + decimation 1/4
                 arm_fir_decimate_f32_Nizar ( &DECIMATE_RX_Q, adb.q_buffer, adb.q_buffer, BS_32 );  // Nizar    // LPF 3.6Khz ( FIR 43taps) + decimation 1/4

				
							//************************************************************************************************
							//************* Rx Hilbert + LPF   2.9 Khz (199taps/12Khz)  USB   LSB   CW   Digital  ************
                			//************************************************************************************************

                 arm_fir_f32_Nizar ( &Fir_Rx_Hilbert_I,  adb.i_buffer,  adb.i_buffer,  BS_8 );   // Hilbert lowpass   0 degrees
                 arm_fir_f32_Nizar ( &Fir_Rx_Hilbert_Q,  adb.q_buffer,  adb.q_buffer,  BS_8 );    // Hilbert lowpass -90 degrees

            }

            switch( dmod_mode )
            {
            	case DEMOD_USB: arm_add_f32 ( adb.i_buffer, adb.q_buffer, adb.a_buffer[0], BS_8 ); break;  // sum of I and Q - USB
  				case DEMOD_LSB:	arm_sub_f32 ( adb.i_buffer, adb.q_buffer, adb.a_buffer[0], BS_8 ); break;  // dif of I and Q - LSB


				case DEMOD_CW:	if ( !ts.cw_lsb )  // CW USB RX mode
								{
									arm_add_f32 ( adb.i_buffer, adb.q_buffer, adb.a_buffer[0], BS_8 );   // sum of I and Q - USB
								}
								else              // CW LSB RX mode
								{
									arm_sub_f32 ( adb.i_buffer, adb.q_buffer, adb.a_buffer[0], BS_8 );   // difference of I and Q - LSB
								}
								break;

				case DEMOD_AM :
				case DEMOD_SAM:	AudioDriver_DemodSAM ( BS_32 );   // lowpass filtering, decimation, and SAM demodulation
								break;

				case DEMOD_FM:
									//************************************************************************************************
									//************* Rx Hilbert + LPF   3.6 Khz ( 89taps/48Khz)  FM ***********************************
									//************************************************************************************************

								arm_fir_f32_Nizar ( &Fir_Rx_Hilbert_I,  adb.i_buffer,  adb.i_buffer,  BS_32 );   // in AM: lowpass filter, in other modes: Hilbert lowpass 0 degrees
								arm_fir_f32_Nizar ( &Fir_Rx_Hilbert_Q,  adb.q_buffer,  adb.q_buffer,  BS_32 );   // in AM: lowpass filter, in other modes: Hilbert lowpass -90 degrees

								AudioDriver_DemodFM ( BS_32 );
								break;

				case DEMOD_DIGI:
														// if we are here, the digital codec (e.g. because of no signal) asked to decode using analog demodulation in the respective sideband
								if (ts.digi_lsb) { arm_sub_f32 ( adb.i_buffer, adb.q_buffer, adb.a_buffer[0], BS_8 );} // difference of I and Q - LSB
								else			 { arm_add_f32 ( adb.i_buffer, adb.q_buffer, adb.a_buffer[0], BS_8 );} // sum of I and Q - USB
								break;

					default:	arm_add_f32 ( adb.i_buffer, adb.q_buffer, adb.a_buffer[0], BS_8 );   // sum of I and Q - USB
								break;
            }



            if ( dmod_mode != DEMOD_FM )       // are we NOT in FM mode?  If we are not, do decimation, filtering, DSP notch/noise reduction, etc.
            {

            	bool  Enhanced_Treatement = ( dmod_mode != DEMOD_CW ) && ( ts.dsp_inhibit == 0 );

            			 //***************************************************************************************************
						 //****** RX  Filtrage Passe HAUT HPF 50Hz, 65Hz, 75Hz, 100Hz, 150Hz, 200Hz  @-3dB PAPOULI ************
						 //***************************************************************************************************

            	if ( Enhanced_Treatement )           // not CW  && ts.dsp_inhibit == 0
            	{
				   if ( IIR_Rx_HP_Filter.numStages > 0 )
				   {
					   arm_iir_lattice_f32_Nizar ( &IIR_Rx_HP_Filter, adb.a_buffer[0], adb.a_buffer[0], BS_8 );
				   }
            	}
						 //************************************************************************************************************
						 // RX filtrage Passe-Bas/ ou passe-bande IIR 10em ordre avancee avant calcul smeter (apres Hilbert Passe_bas 3.6 Khz)
						 //*************************************************************************************************
								// Apply audio  bandpass filter IIR  2.3Khz, 2.5Khz, 2.7Khz,  2.9Khz .. NIZZZ  a �t� deplac�  ici plus haut avant mesure peak Smeter
            					// a essayer de faire des IIR � moindre ordre pour moins de calcul et moins de distorsion de phase

                if (  (IIR_PreFilter[0].numStages > 0) )   // yes, we want an audio IIR filter     ... LPF 2.3Khz, 2.5Khz , 2.7Khz,  2.9Khz ..
				{
					arm_iir_lattice_f32_Nizar ( &IIR_PreFilter[0], adb.a_buffer[0], adb.a_buffer[0], BS_8 );
				}

                			//*****************************************************************
                            //********************* RX  Peak Smeter *****************************
                            //*****************************************************************

                if ( pass_count & 0x1u )  // 1..3. on evalue le Peak-Smeter une fois sur deux pour reduire le volume du calcul mcu  12Khz ici
                {
					if ( (dmod_mode == DEMOD_USB) || (dmod_mode == DEMOD_LSB) || ( dmod_mode == DEMOD_CW ))
					{
						                        // N96K P0-3: abs() -> fabsf() for float32_t magnitudes (audit A3)
						                        float32_t  somme_a0  = fabsf( adb.a_buffer[0][0] );
												float32_t  somme_a1  = fabsf( adb.a_buffer[0][1] );
												float32_t  somme_a2  = fabsf( adb.a_buffer[0][2] );
												float32_t  somme_a3  = fabsf( adb.a_buffer[0][3] );
														   somme_a0 += fabsf( adb.a_buffer[0][4] );
														   somme_a1 += fabsf( adb.a_buffer[0][5] );
														   somme_a2 += fabsf( adb.a_buffer[0][6] );
														   somme_a3 += fabsf( adb.a_buffer[0][7] );
														   somme_a0 += somme_a1;
														   somme_a2 += somme_a3;
														   somme_a0 += somme_a2;
						float32_t                          diff_peak = somme_a0 - ts.Sm_peak_moyen;
						
						if (diff_peak > 0) {  ts.Sm_peak_moyen += diff_peak * 0.029; }  // 0.025 rapide en montee
						else               {  ts.Sm_peak_moyen += diff_peak * 0.016; }  // 0.014 plus lent en descente
					}
                }





			/*	if ( ts.show_debug_info )
				{
					 arm_iir_lattice_f32_Nizar ( &IIR_RX_PASS_TOUT_Filter, adb.a_buffer[0],  adb.a_buffer[0], BS_8 );   // Filtre passe tout microphone 137Hz
				} */

					//*****************************************************************************
					//*************************** RX  AGC  Audio  *********************************
					//*****************************************************************************
					// now process the samples and perform the receiver AGC function

				if ( !ts.dsp_inhibit )
				{
					if ( ! dsp_delay_ok )  // necessaire pour une reprise rapide audio au switch tX-->Rx //  1.0 + ts.agc_wdsp_hang_enable * 0.1
					{
						if (ts.agc_wdsp_slope)  { AudioDriver_RxAgcWdsp ( BS_8, adb.a_buffer[0] ); }
						else                    { arm_scale_f32( adb.a_buffer[0], ts.float_Fix_audio_Gain, adb.a_buffer[0], BS_8 ); }  // apply fixed RF Gain
					}
					else dsp_delay_ok--;
				}
				else {  if ( dmod_mode != DEMOD_CW ) dsp_delay_ok = 20; }  // c'est un delay de 30 * 0.6666 ms = 20ms pour eviter les clics
				 // pour creer un petit delay pour ne pas impulser le AGC
               	//****************************************************************************
				//************************   RX  Basse Equalizeur  , biquad Filtre   fs = 12 Khz  *********************
				//****************************************************************************
				// a essayer d'inhiber cette  fonction en cas ou le coeff equalis = 0 ; et voir son effet sur le spectre sur PC
                // 	la Basse equalizer  est positionné après le AGC et avant le DNR (Noise Reduction)

			/*	if ( Enhanced_Treatement )    // not CW  && ts.dsp_inhibit == 0
				{
						if ( ts.bass_gain )  // pour suspendre cette tache occupante non prioritaire en cas de besoin , lors des touches tactiles
						{
							arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_Rx_Equaliser_Bass_decim, adb.a_buffer[0], adb.a_buffer[0], BS_8 );
						}
				}*/

					   //***************************************************************************************
					   //********************** RX  LMS  Automatic  Notch  *************************************
					   //***************************************************************************************

			                // DSP Automatic Notch Filter using LMS (Least Mean Squared) algorithm

				if ( ( dsp_active & DSP_NOTCH_ENABLE ) )
				{
					if  ( Enhanced_Treatement && !dsp_delay_ok )       // not CW  && ts.dsp_inhibit == 0
					{
						if ( !( dmod_mode == DEMOD_SAM && (FilterPathInfo[ts.filter_path].sample_rate_dec) == RX_DECIMATION_RATE_24KHZ))       // No notch in CW
						{
							AudioDriver_NotchFilter ( BS_8, adb.a_buffer[0] );  // Do automatic notch filter
						}
					}
				}




                 //*****************************************************************************
                 //************************* RX  NR  ( Digital  Noise Reduction ) **************
                 //*****************************************************************************

                static  uint16_t   delay_after_NR_restart = 25;    // au redemarrage du Noise-Reduction, parfois on lui  demande
                static  bool       NR_standby             = true;  // de reinitialiser ses parametres, ainsi il peut  faire planter
                                                                   // les filtres iir qui suivent, donc on les suspend un peu de temps a chaque
                                                                   // redemarrage NR avant de les remettre  (egualiseur et autres ...)

				if ( Enhanced_Treatement && ( dsp_active & DSP_NR_ENABLE ) && ts.dsp_nr_strength )  // cela bloque le son a la commutation // not CW  && ts.dsp_inhibit == 0
				{
					if ( NR_standby ) { NR_standby = false;  delay_after_NR_restart = 25; }
					AudioDriver_RxProcessorNoiseReduction ( BS_8, adb.a_buffer[0] );


							// NR_in and _out buffers are using the same physical space than the freedv_iq_buffer in a
							// shared MultiModeBuffer union.
							// for NR reduction we use a maximum of 256 real samples
							// so we use the freedv_iq buffers in a way, that we use the first half of each array for the input
							// and the second half for the output
							// .real and .imag are loosing there meaning here as they represent consecutive real samples
				} // end of new nb
				else
				{ NR_standby = true; };

				if ( delay_after_NR_restart )  delay_after_NR_restart--; // protection contre les signaux bloquants au démarrage du NR


                //**********************************************************************************
                //************************  RX  AM / SAM  Audio SCALING  ***************************
                //**********************************************************************************
                			// Calculate scaling based on decimation rate since this affects the audio gain
							// Scale audio according to AGC setting, demodulation mode and required fixed levels and scaling

                if ( dmod_mode== DEMOD_AM || dmod_mode== DEMOD_SAM )  // *1.95 AM/SAM  *1.3 SSB CW Digital
                {
                	arm_scale_f32 ( adb.a_buffer[0], 1.5, adb.a_buffer[0], BS_8 );  // apply fixed amount of audio gain scaling to make the audio levels correct along with AGC
                }


                if (( delay_after_NR_restart == 0 ) && (ts.dsp_inhibit == 0)) // Si delay ecoule apres dernier redemarrage du NR Noise-Reduction
                {
                	//*****************************************************************************
					//**************************** RX  CW  Decodeur & Tune Helper on SAM, AM  *****
					//*****************************************************************************

					if (dmod_mode == DEMOD_CW || dmod_mode == DEMOD_AM || dmod_mode == DEMOD_SAM)
										// switch to use TUNE HELPER in AM/SAM
					{
						// pour ne pas faire planter le mcu probablement par manque de memoire disponible lors de touchscreen

						 // if ( ts.switch_pause < 70)

							 CwDecode_RxProcessor ( adb.a_buffer[0], BS_8 ); // 150

						//*************************************************************************************************
						//*********************************** Rx DSP Manuel Peak detector only on CW  *********************
						//*************************************************************************************************

						if ( dmod_mode == DEMOD_CW  )
						{
							if ( dsp_active & DSP_MPEAK_ENABLE )   //   && ( ts.dsp_inhibit == 0) )
							{
								arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_Rx_Manuel_Peak_decim, adb.a_buffer[0], adb.a_buffer[0], BS_8 );
							}
						}
					}
					//****************************************************************************
					//******************* RX  RTTY Decodeur *****************************************
					//****************************************************************************

					else if ( is_demod_rtty() )
					{
						AudioDriver_RxProcessor_Rtty ( adb.a_buffer[0], BS_8 );
					}

					//*****************************************************************************
					//***************************** RX  PSK Decodeur *********************************
					//*****************************************************************************

					else if (is_demod_psk() )
					{
						AudioDriver_RxProcessor_Bpsk ( adb.a_buffer[0], BS_8 );
					}




					//************************************************************************************************
					//************************************ Rx DSP  Manuel Notch Filter  **********************
					//************************************************************************************************

					if  ( Enhanced_Treatement )                // not CW  && ts.dsp_inhibit == 0
					{
						if ( dsp_active & DSP_MNOTCH_ENABLE )
						{
							arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_Rx_Manuel_Notch_decim, adb.a_buffer[0], adb.a_buffer[0], BS_8 );
						}



					//****************************************************************************
					//************************   RX  Basse Equalizeur  , biquad Filtre   fs = 12 Khz  *********************
					//****************************************************************************
					// a essayer d'inhiber cette  fonction en cas ou le coeff equalis = 0 ; et voir son effet sur le spectre sur PC
					// 	la Basse equalizer  est positionné après le AGC et avant le DNR (Noise Reduction)


						if ( ts.bass_gain )  // pour suspendre cette tache occupante non prioritaire en cas de besoin , lors des touches tactiles
						{
							arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_Rx_Equaliser_Bass_decim, adb.a_buffer[0], adb.a_buffer[0], BS_8 );
						}

						//*****************************************************************************************************
						//*******************************  RX  Treble  Equaliseur  Quad Filter  fs=12Khz ************************
						//*****************************************************************************************************
										   // this is the biquad filter, a highshelf filter ( ***  egualiseur  treble **** )
										   // a inhiber cette fonction en cas ou le coeff est egual a zero, cela nous fera gagner un temps de calcul processeur MCU


						if ( ts.treble_gain )  // pour suspendre cette tache occupante non prioritaire en cas de besoin , lors des touches tactiles
						{
							arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_Rx_Treble_decim,  adb.a_buffer[0],  adb.a_buffer[0],  BS_8  );
						}

					} // End Enhancement


                }  // End  (delay_after_NR_restart == 0)





							//*******************************************************************************
							//**************************** Rx Interpolation 12Khz ---> 48Khz ****************
                            //************************** FIR tap=16  3860Hz LPF  rise cosin FS=48Khz ********
							//*******************************************************************************
							//   resample back to original sample rate while doing low-pass filtering to minimize audible aliasing effects

                if ( Rx_FIR_INTERPOLATE_12_to_48.phaseLength > 0 )
				{
					arm_fir_interpolate_f32_Nizar ( &Rx_FIR_INTERPOLATE_12_to_48, adb.a_buffer[0], adb.a_buffer[1], BS_8 ); // buffer destination doit etre different du buffer source
				}

							//********************************************************************************
							//*** RX Filtrage IIR suplementaire anti-repliement si necessaire pres interpolation 48Khz**
							//********************************************************************************
							// additional antialias filter for specific bandwidths, IIR ARMA-type lattice filter
				/*  // plus besoin , on a uniformiser la configuration des filtres pour les fc > 3.2Khz

				if (( IIR_AntiAlias[0].numStages > 0 ) && (delay_after_NR_restart == 0))   // yes, we want an interpolation IIR filter a fs=48Khz
				{
					arm_iir_lattice_f32_Nizar ( &IIR_AntiAlias[0], adb.a_buffer[1], adb.a_buffer[1], BS_32 );
				} */

							//********************************************************************************
							//********************************************************************************

            } // end NOT( FM mode)


            				//*************************************************************************************************
                        	//******************************  RX   FM    FM    FM    FM    ************************************
                        	//*************************************************************************************************
            else  // if ( dmod_mode == DEMOD_FM )     // FM : we don't do any decimation, interpolation, filtering or any other processing - just rescale audio amplitude
            {

                   			//**********************  Scaling FM output **************************************
                arm_scale_f32 ( adb.a_buffer[0], RadioManagement_FmDevIs5khz() ? FM_RX_SCALING_5K : FM_RX_SCALING_2K5, adb.a_buffer[1],	BS_32);  // apply fixed amount of audio gain scaling to make the audio levels correct along with AGC

                           // HPF 22Hz  + LPF 2000 Hz filtrage pour mieux adoucire l'écoute audio finale de la FM
                if ( tx_audio_source != TX_AUDIO_DIG_EX )
                {
                	//************************* Filtre Audio passe Haut 22Hz 1er ordre FS=48Khz ****************
                	arm_iir_lattice_f32_Nizar ( &IIR_Rx_HP_Filter_22Hz, adb.a_buffer[1], adb.a_buffer[1], BS_32 );

                	//************************** Filtre Audio Passe Bas 2000 Hz 3em ordre FS=48Khz ****************
                	arm_iir_lattice_f32_Nizar ( &IIR_Rx_LP_Filter_2K6,  adb.a_buffer[1], adb.a_buffer[1], BS_32 );
                }

                			//*****************************************************************************************************
							//*******************************  RX Equaliseur Treble biquad *******************************************
							//*****************************************************************************************************
								   // this is the biquad filter, a highshelf filter ( ***  egualiseur  treble **** )
								   // a inhiber cette fonction en cas ou le coeff est egual a zero, cela nous fera gagner un temps de calcul processeur MCU
								   // aussi a placer cette fonction avant interpolation pour gagner en temps de calcul tout en la gardant pour FM

                if ( ts.treble_gain  )   // Filtre FS=48Khz
				{
					if ( ts.paddles_active)   // pour suspendre cette tache occupante non prioritaire en cas de besoin, lors des touches tactiles
					{
						arm_biquad_cascade_df1_f32_Nizar ( &IIR_biquad_Rx_Equaliser_Treble_48Khz,  adb.a_buffer[1],  adb.a_buffer[1],  BS_32 );
					}
				}
            }



        }  //  *** END If Not  FREEDV



			///**************************************************************************************************
			///***********************************     Rx  Volume Scaling  **************************************
			/// *************************************************************************************************

					//    VARIABLE LEVEL FOR SPEAKER
					//    LINE OUT (constant level : le niveau gain audio Line-Out est command� directement sur le hardware du Codec )
				    //    Do fixed scaling of audio for LINE OUT and copy to "a" buffer in one operation
					//    Ligne masqu�e pour simplifier  Line-Out = Haut-parleur Out
					//    AF gain in "ts.audio_gain-active" : //  0 - 16: via codec command  // 17 - 20: soft gain after decoder

        float32_t   coeff_16_to_32bit_DAC = 65536;

    	float32_t   usb_audio_gain = ( ts.rx_gain[RX_AUDIO_DIG].value * ( 4.5e-6) ); // *10/24.0 // *10/31.0;    /// Gain  vers USB Cable interface vers PC

    	if ( ts.rx_gain[ RX_AUDIO_SPKR ].value >  CODEC_SPEAKER_MAX_VOLUME ) // 40 = 16*2.5 is volume control above highest hardware setting?
        {
            // arm_scale_f32 ( adb.a_buffer[1], (float32_t) ts.rx_gain[ RX_AUDIO_SPKR ].active_value, adb.a_buffer[1], BS_32 );    // yes, do software volume control adjust on "b" buffer

    		coeff_16_to_32bit_DAC *=  ts.rx_gain[RX_AUDIO_SPKR].active_value;

            usb_audio_gain /= (float32_t) ts.rx_gain[RX_AUDIO_SPKR].active_value;   // pour ajuster le gain sortie vers cable USB
        }


		  ///***************************************************************************************************
		  ///******************************    Rx    OUTPUTS     ***********************************************
		  ///***************************************************************************************************

    	//*******************************************************************************************************
		///************************ Rx  Adding BEEP Output   ****************************************************
		///******************************************************************************************************

    	if (ts.beep_active)
    	{
			for ( int i = 0; i < BS_32; i++ )                            // transfer to DMA buffer and do conversion to INT
			{

				if (ads.beep.step)       // is beep active? Calculate next sample and shift accumulator to index sine table
				{
					adb.a_buffer[1][i] += (float32_t) softdds_nextSample ( &ads.beep ) * ads.beep_loudness_factor; // load indexed sine wave value, adding it to audio, scaling the amplitude and putting it on "b" - speaker (ONLY)
				}
				else                    // beep not active - force reset of accumulator to start at zero to minimize "click" caused by an abrupt voltage transition at startup
				{
					ads.beep.acc = 0;
				}
			}
    	}


    	//********************************************************************************************************
		//********* Rx Scale 16-->32bit et Interpolation 48--> 96Khz  avant Haut Parleur ************************
		//********************************************************************************************************

    //	arm_scale_interpolate_2x_f32_Nizar ( adb.a_buffer[1], coeff_16_to_32bit_DAC, ii_buffer, BS_32 );  // * 65536;

		arm_scale_Mid_interpolate_f32_Nizar ( adb.a_buffer[1], coeff_16_to_32bit_DAC, ii_buffer, BS_32 );  // * 65536;



		if (( tx_audio_source == TX_AUDIO_LINEIN_L ) || ( tx_audio_source == TX_AUDIO_LINEIN_R ))  // PACTOR ou equivalent
		{

			//********************************************************************************************************
			//********* Rx Filtre LPF 11.5Khz IIR 2em Anti-Alias après Interpolat 48Khz-->96Khz vers Haut paleur *****
			//********************************************************************************************************

		    //	arm_iir_lattice_f32_Nizar ( &IIR_Rx_Interpo_48_to_96,  ii_buffer,  ii_buffer, BS_64 );

			//********************************************************************************************************
			//***************  Rx  OUTPUT to Speaker and LineOut Avec  96Khz  ****************************************
			//********************************************************************************************************

			for ( int i = 0; i < BS_64; i++ )                            // transfer to DMA buffer and do conversion to INT
			{
				 //**********************  LINE-OUT != HAUT-PARLEUR   **********************
				  int32_t   iii   =  ii_buffer[i];

				  dst_Codec[i ].l = I2S_correctHalfWord ( iii       );    //      Haut Parleur
			 	  dst_Codec[i ].r = I2S_correctHalfWord ( iii << 3u );	  // *8   Line-OUT   for Pactor Modem

			} // for end
		}

		else
		{

			for ( int i = 0; i < BS_64; i++ )                            // transfer to DMA buffer and do conversion to INT
			{
				 //**********************  LINE-OUT != HAUT-PARLEUR   **********************
				  int32_t   iii   =  ii_buffer[i];
				  dst_Codec[i ].r =  dst_Codec[i ].l = I2S_correctHalfWord ( iii );    //      Haut Parleur

			} // for end
		}
	     //*************************************************************************************************
		 //************* Rx Send audio to USB Cable, but not in DIG-I/Q Mode,  *****************************
		 //*************************************************************************************************




	 	if (ts.Audio_Card_mchf_playing ) // Si en cours de saisie audio par carte son generique alors charger l'audio
		{
			 if ( tx_audio_source != TX_AUDIO_DIG_IQ )  // 0:inactive  1:active 2:playing 3:paused  4:stopped  5:error
			 {
				 arm_scale_f32 (  ii_buffer , usb_audio_gain, ii_buffer,  BS_64  );

				for ( int i = 0; i < BS_64; i++ )
				{
					int16_t   vals =  ii_buffer[i];
					audio_USB_2_output_buffer ( vals );
				}
			 }
		}

    }  // end if_not_mute_Loop


    		//**************************************************************************************************
        	//**************************** Rx  Temporisateur d'affichage ***************************************
            //**************************************************************************************************

	if ( ts.RFG_wait > 1 )
	{
		//if (ts.enc_two_mode != ENC_TWO_MODE_NR)

		if ( (ts.enc_two_mode==ENC_TWO_MODE_BASS_GAIN) || (ts.enc_two_mode==ENC_TWO_MODE_TREBLE_GAIN) ) ts.RFG_wait--;
	}   // temporisateur pour activation du pav�  CAG  de nouveau

	if ( ts.AFG_wait > 1 ) ts.AFG_wait--; // temporisateur pour activation du pav�  VOL  de nouveau
}



///*************************************************************************************************

static   float32_t   AudioDriver_absmax ( float32_t*  buffer, int  size )
{
    float32_t   min,        max;
    uint32_t    pindex;

    arm_max_f32 ( buffer, size, &max, &pindex );      // find absolute value of audio in buffer after gain applied
    arm_min_f32 ( buffer, size, &min, &pindex );

    return   -min>max?  -min : max;
}

//**************************************************************************************************

static void AudioDriver_Tx_Reverberation ( float32_t* buffer)  // fonction de reverberation Tx Mic
{
	static     float32_t  buff_eco[384];  // 64 ms max delay
	static     float32_t  last_val        = 0;
	static     uint16_t   indice_enrg_eco = 0;
	static     uint16_t   indice_lect_eco = 4;
               float32_t  coeff_entier    = (float32_t) ts.reverberation_rate  * 0.01;
               float32_t  coeff_moitie    = coeff_entier * 0.5;

	buffer[0] += ( last_val                        + buff_eco[ indice_lect_eco ]) * coeff_moitie;
	buffer[1] +=   buff_eco[ indice_lect_eco     ]                                * coeff_entier;
	buff_eco[ indice_enrg_eco++ ] = buffer[0];
	// N96K P0-3: was "buff_eco[(indice_lect_eco++)] + buff_eco[indice_lect_eco]", which reads and
	// modifies indice_lect_eco without a sequence point (UB, audit A2). Now the two taps are read
	// explicitly (n and n+1) and the step is explicit; tap pairs, 4 increments/call and the wrap
	// check are unchanged. buffer[3]/[5]/[7] below read indice_lect_eco == n+1 exactly as before.
	buffer[2] += ( buff_eco[ indice_lect_eco ] + buff_eco[ indice_lect_eco + 1u ]) * coeff_moitie;
	indice_lect_eco++;   // N96K P0-3: explicit step 1 of 4 (audit A2)
	buffer[3] +=   buff_eco[  indice_lect_eco    ]                                * coeff_entier;
	buff_eco[ indice_enrg_eco++ ] = buffer[2];
	buffer[4] += ( buff_eco[ indice_lect_eco ] + buff_eco[ indice_lect_eco + 1u ]) * coeff_moitie;
	indice_lect_eco++;   // N96K P0-3: explicit step 2 of 4 (audit A2)
	buffer[5] +=   buff_eco[  indice_lect_eco    ]                                * coeff_entier;
	buff_eco[ (indice_enrg_eco++) ] = buffer[4];
	buffer[6] += ( buff_eco[ indice_lect_eco ] + buff_eco[ indice_lect_eco + 1u ]) * coeff_moitie;
	indice_lect_eco++;   // N96K P0-3: explicit step 3 of 4 (audit A2)
	buffer[7] +=   buff_eco[  indice_lect_eco    ]                                * coeff_entier;
	buff_eco[ (indice_enrg_eco++) ] = buffer[6];
	last_val   =   buff_eco[ indice_lect_eco ];   // N96K P0-3: read then increment (was (indice_lect_eco++))
	indice_lect_eco++;   // N96K P0-3: explicit step 4 of 4 (audit A2)
	uint16_t   delayy = ts.reverberation_delay << 2u;
	if ( indice_enrg_eco >= delayy )  indice_enrg_eco = 0;
	if ( indice_lect_eco >= delayy )  indice_lect_eco = 0;
}

/**
 * @brief audio speech compressor (look-ahead type) by KA7OEI
 * @param buffer input (and output) buffer for audio samples
 * @param blockSize number of samples to process
 * @param gain_scaling scaling applied to buffer
 *
 */
/*
static  void  AudioDriver_TxCompressor ( float32_t* buffer,  int16_t blockSize,  float gain_scaling)
{
    static uint32_t	   alc_delay_inbuf = 0,
    		           alc_delay_outbuf   ;

    if (ts.tx_comp_level > -1)  // CMP Not "OFF"
    {
        if(!ts.tune)        // do post-filter gain calculations if we are NOT in TUNE mode
        {
            // perform post-filter gain operation ,  this is part of the compression
            float32_t gain_calc = ((float32_t)ts.alc_tx_postfilt_gain_var)/2.0 + 0.5 ;

            // get post-filter gain setting , offset it so that 2 = unity
            arm_scale_f32(buffer, gain_calc, buffer, blockSize);      // use optimized function to apply scaling to I/Q buffers
        }

        // Do ALC processing on audio buffer - look-ahead type by KA7OEI
        if (false)
        {
            arm_fill_f32(ALC_VAL_MAX * gain_scaling, adb.agc_valbuf, blockSize);
        }
        else
        {
            // since both values are marked as volatile, we copy them before using them, saves some cpu cycles.
            static  float32_t     alc_val;
            alc_val             = ads.alc_val;
            float32_t alc_decay = ads.alc_decay;

            for(uint16_t i = 0; i < blockSize; i++)
            {
                // perform ALC on post-filtered audio (You will notice the striking similarity to the AGC code!)

                // calculate current level by scaling it with ALC value
                float32_t alc_var  =  fabsf(buffer[i] * alc_val)/ALC_KNEE - 1.0; // calculate difference between ALC value and "knee" value
                if(alc_var < 0)	 	// is audio below ALC "knee" value?
                {
                    // alc_var is a negative value, so the resulting expression is negative
                    // but we want to increase the alc_val -> we subtract it
                    alc_val -= alc_val * alc_decay * alc_var;	// (ALC DECAY) Yes - Increase gain slowly
                }
                else
                {
                    // alc_var is a positive value
                    alc_val    -= alc_val * ALC_ATTACK * alc_var;          // Fast attack to increase gain
                    if(alc_val  < ALC_VAL_MIN) { alc_val = ALC_VAL_MIN; }  // Prevent zero or "negative" gain values

                }
                if (alc_val  > ALC_VAL_MAX)	  {  alc_val = ALC_VAL_MAX;  }  //  limit to fixed values within the code


                adb.agc_valbuf[i] = (alc_val * gain_scaling);	// store in "running" ALC history buffer for later application to audio data
            }

            // copy final alc_val back into "storage"
            ads.alc_val = alc_val;

        }

        // Delay the post-ALC audio slightly so that the ALC's "attack" will very slightly lead the audio being acted upon by the ALC.
        // This eliminates a "click" that can occur when a very strong signal appears due to the ALC lag.  The delay is adjusted based on
        // decimation rate so that it is constant for all settings.

        // Update the in/out pointers to the ALC delay buffer
        alc_delay_inbuf  +=  blockSize;
        alc_delay_outbuf  =  alc_delay_inbuf + blockSize;
        alc_delay_inbuf  %=  AUDIO_DELAY_BUFSIZE;
        alc_delay_outbuf %=  AUDIO_DELAY_BUFSIZE;

        arm_copy_f32(buffer                               , &audio_delay_buffer[alc_delay_inbuf], blockSize);	// put new data into the delay buffer
        arm_copy_f32(&audio_delay_buffer[alc_delay_outbuf], buffer                              , blockSize);	// take old data out of the delay buffer

        arm_mult_f32(buffer, adb.agc_valbuf, buffer, blockSize);		// Apply ALC gain corrections to TX audio channels
    }
}  */


/*
void    Niz_mult_f32 (  float32_t * pSrcA,  float32_t * pSrcB,  float32_t * pDst)
{
  float32_t  inA1, inA2, inA3, inA4;
  float32_t  inB1, inB2, inB3, inB4;
  float32_t  out1, out2, out3, out4;

    inA1 = * pSrcA;                             inB1 = * pSrcB;
    inA2 = *(pSrcA + 1);                        inB2 = *(pSrcB + 1);  out1 = inA1 * inB1;
    inA3 = *(pSrcA + 2);                        inB3 = *(pSrcB + 2);  out2 = inA2 * inB2;      * pDst      = out1;
    inA4 = *(pSrcA + 3);                        inB4 = *(pSrcB + 3);  out3 = inA3 * inB3;      *(pDst + 1) = out2;
    inA1 = *(pSrcA + 4);                        inB1 = *(pSrcB + 4);  out4 = inA4 * inB4;      *(pDst + 2) = out3;
    inA2 = *(pSrcA + 5);                        inB2 = *(pSrcB + 5);  out1 = inA1 * inB1;      *(pDst + 3) = out4;
	inA3 = *(pSrcA + 6);                        inB3 = *(pSrcB + 6);  out2 = inA2 * inB2;      *(pDst + 4) = out1;
	inA4 = *(pSrcA + 7);                        inB4 = *(pSrcB + 7);  out3 = inA3 * inB3;      *(pDst + 5) = out2;
																	  out4 = inA4 * inB4;      *(pDst + 6) = out3;
																	  	  	  	  	  	  	   *(pDst + 7) = out4;
}

*/

/*
//  *****************************************************************************************************
 #define  C_max   35700
 #define  C_min  -35700
//  *****************************************************************************************************
static void    Niz_mult_f32_saturation_35500 (  float32_t * pSrcA,  float32_t * pSrcB,  float32_t * pDst )  // 8 echantillons en mode d�cimation 12Khz
{

 // float32_t  C_max =  30000 + (float32_t) (ts.beep_loudness)*500;
 // float32_t  C_min = -30000 - (float32_t) (ts.beep_loudness)*500;

  float32_t  inA1, inA2, inA3, inA4;
  float32_t  inB1, inB2, inB3, inB4;
  float32_t  out1, out2, out3, out4;

    inA1 = * pSrcA;                             		inB1 = * pSrcB;
    inA2 = *(pSrcA + 1);                        		inB2 = *(pSrcB + 1);  out1 = inA1 * inB1;
    inA3 = *(pSrcA + 2);  if(out1<C_min) out1=C_min;    inB3 = *(pSrcB + 2);  out2 = inA2 * inB2;  if (out1>C_max)  out1=C_max;
    inA4 = *(pSrcA + 3);  if(out2<C_min) out2=C_min; 	inB4 = *(pSrcB + 3);  out3 = inA3 * inB3;  if (out2>C_max)  out2=C_max;   * pDst      = out1;
    inA1 = *(pSrcA + 4);  if(out3<C_min) out3=C_min; 	inB1 = *(pSrcB + 4);  out4 = inA4 * inB4;  if (out3>C_max)  out3=C_max;   *(pDst + 1) = out2;
    inA2 = *(pSrcA + 5);  if(out4<C_min) out4=C_min;    inB2 = *(pSrcB + 5);  out1 = inA1 * inB1;  if (out4>C_max)  out4=C_max;   *(pDst + 2) = out3;
	inA3 = *(pSrcA + 6);  if(out1<C_min) out1=C_min;    inB3 = *(pSrcB + 6);  out2 = inA2 * inB2;  if (out1>C_max)  out1=C_max;   *(pDst + 3) = out4;
	inA4 = *(pSrcA + 7);  if(out2<C_min) out2=C_min;    inB4 = *(pSrcB + 7);  out3 = inA3 * inB3;  if (out2>C_max)  out2=C_max;   *(pDst + 4) = out1;
						  if(out3<C_min) out3=C_min;						  out4 = inA4 * inB4;  if (out3>C_max)  out3=C_max;   *(pDst + 5) = out2;
						  if(out4<C_min) out4=C_min;											   if (out4>C_max)  out4=C_max;	  *(pDst + 6) = out3;
						  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  	  *(pDst + 7) = out4;
}
*/

//*****************************************************************************************************
/*
static  void  AudioDriver_Tx_Noise_Gate_Process ( float32_t* buffer)
{
	static  uint8_t       start_buf                   = 0 ;
	static  float32_t     Buff_Noise[16];
	static  bool          current_signal              = false;
    static  float32_t     cumulateur_avg              = 0;
	        float32_t     cumulateur                  = 0;

	arm_copy_f32 ( buffer,  &Buff_Noise[ start_buf ],   8 );      start_buf += 8;  start_buf &= 0xF;

	for ( uint16_t i = 0; i < 8; i++ ) 	{ cumulateur += fabsf( buffer[i] ); }
	cumulateur          /= 8;
	cumulateur_avg      += ( -cumulateur_avg + cumulateur ) * 0.5;
	float32_t   val_cmp  = current_signal ? cumulateur_avg : cumulateur ;

	if (  val_cmp < ts.Noise_Gate_Thresh )
	{
		if (current_signal) { arm_copy_f32 ( &Buff_Noise[ start_buf ],  buffer,  8  ); }
		else                { memset       ( buffer, 0 , 32  );                        }
		ads.alc_val    =  1;
		current_signal = false;
	}
	else
	{
		arm_copy_f32 ( &Buff_Noise[ start_buf ],  buffer,  8  );
		current_signal = true;
	}
} */



//*****************************************************************************************************
//*****************************************************************************************************
/*
static  void  AudioDriver_Tx_Noise_Gate_Process ( float32_t* buffer )
{

    static  float32_t     cumulateur_avg              = 0;
	        float32_t     cumulateur                  = 0;



	for ( uint16_t i = 0; i < 8; i++ ) 	{ cumulateur += fabsf( buffer[i] ); }
	cumulateur          /= 8;
	cumulateur_avg      += ( -cumulateur_avg + cumulateur ) * 0.20;

	if (  cumulateur_avg < ts.Noise_Gate_Thresh )
	{
		memset ( buffer, 0 , 32  );
		ads.alc_val    =  1;
	}
}
*/

//*****************************************************************************************************
//*****************************************************************************************************

/*
static   void   AudioDriver_Tx_Noise_Gate_Process ( float32_t*  buffer )    // algorithme final
{
	static  uint8_t       start_buf              = 0 ;
	static  float32_t     Buff_Noise[16];
	static  bool          current_signal         = false;
    static  float32_t     cumul_avg_to_stop      = 0;
    static  float32_t     cumul_avg_to_start     = 0;
	        float32_t     cumulateur             = 0;

	arm_copy_f32 ( buffer,  &Buff_Noise[ start_buf ],   8 );      start_buf += 8;  start_buf &= 0xF; // retard de 0.666 ms

	for ( uint16_t i = 0; i < 8; i++ ) 	{ cumulateur += fabsf( buffer[i] ); }
	cumulateur              /= 8;
	cumul_avg_to_stop  += ( -cumul_avg_to_stop  + cumulateur ) * 0.27;    // Moyennage lent
	cumul_avg_to_start += ( -cumul_avg_to_start + cumulateur ) * 0.45;    // Moyennage rapide

	float32_t   val_cmp      = current_signal ? cumul_avg_to_stop : cumul_avg_to_start ;

	if (  val_cmp < ts.Noise_Gate_Thresh )
	{
		if (current_signal) { arm_copy_f32 ( &Buff_Noise[ start_buf ],  buffer,  8  );  current_signal =  false; }
		else                { arm_fill_f32 ( 0, buffer, 8  );                           ads.alc_val    =  1;     }
	}
	else
	{
		arm_copy_f32 ( &Buff_Noise[ start_buf ],  buffer,  8  );

		if ( current_signal == false )
		{
			if ( cumul_avg_to_stop < cumul_avg_to_start)   { cumul_avg_to_stop = cumul_avg_to_start + 80;}
			current_signal = true;
		}
	}
}
*/

//******************************************************************************************************

/*
static  void   Tx_Log_Compressor  (  float32_t*  buffer,  int16_t  blockSize )
{
	for (uint i= 0; i <blockSize; i++ )
	{
		float32_t      val = buffer[i];
		float32_t  abs_val = fabsf( val );

		if ( abs_val > 15000)
		{
			if     (abs_val < 20000)
			{
				if    (val > 0) { val =  15000 + 0.75*(val -15000);}
				else            { val = -15000 + 0.75*(val +15000);}
			}
			else if (abs_val < 25000)
			{
				if    (val > 0) { val =  18750 + 0.50*(val - 20000);}
				else            { val = -18750 + 0.50*(val + 20000);}
			}
			else
			{
				if    (val > 0) { val =  21250 + 0.25*(val - 25000);}
				else            { val = -21250 + 0.25*(val + 25000);}
			}

			buffer[i] = val;
		}
	}
}
*/

//*****************************************************************************************************

static  void  AudioDriver_TxCompressor ( float32_t* buffer,  int16_t  blockSize,  float32_t  modulation_scaling )  // 48Khz et 12Khz aussi
{
    static  uint32_t	 alc_delay_inbuf  = 0,
    		             alc_delay_outbuf    ;
    // N96K B3: geometry (ring length / block size) of the last call, used to re-align the ring
    // indices when the TX path changes (see the ALC delay buffer block further down).
    static  uint16_t     alc_delay_ring_len = 0,
                         alc_delay_block    = 0;

    if ( ts.tx_comp_level == 1 )     // ComPressor Not "OFF"
    {
    	// Le gain est actuellement fait d�s la saisie du buffer , ceci pour gagner du temps processeur
       /* if(!ts.tune)
        {
            float32_t  gain_calc = ( (float32_t) ts.alc_tx_postfilt_gain_var ) / 10.0 + 0.9 ;
            arm_scale_f32 ( buffer, gain_calc, buffer, blockSize );
        }*/


     	static  float32_t     alc_agc;
     	        float32_t     alc_agc_min                 = 0.99;  /// NIZZZ
		                      alc_agc                     = ads.alc_val;
		        float32_t     coeff_attack                = ts.tx_decimatedIQ? 0.1520 : 0.070;   // 0.143 : 0.065;   // 0.11 : 0.065 // ts.tx_decimatedIQ = ( (USB + LSB) & not_DIG_IQ )
		        float32_t     alc_decay                   = ts.tx_decimatedIQ? 0.0190 : 0.012;   //0.0185 // 0.052// 0.045 : 0.010;   // ads.alc_decay;
		        float32_t     alc_final_output_level_gain = modulation_scaling;

		if ( alc_final_output_level_gain > 0.999 ) { alc_final_output_level_gain = ts.tx_comp_out_gain_float; } // gain for SSB mode only

	 // if ( ts.tx_decimatedIQ )  alc_decay *= 4.40;     // 4.3 ;   //   (1.0 + (float) ts.beep_loudness/6.0) ; //  4 ;
  	 //   coeff_attack = (float) (ts.beep_loudness + 5) / 100.0;

	//	  alc_decay  =  (float32_t) ts.agc_wdsp_tau_decay[ ts.agc_wdsp_mode ] / 20000.0;

		float32_t  limit_inf  = 0.125;
		float32_t  limit_sup  = 1.0;
		int32_t    out_target = 30000;

		for ( uint16_t  i = 0; i < blockSize; i++ )
		{
			// cumulateur += fabsf( buffer[i] ) ;

			float32_t            alc_var  = fabsf( buffer[i]        * alc_agc)/out_target    - limit_sup; // calculate difference between ALC value and "knee" value 30000
			if ( alc_var < 0 ) { alc_agc -= alc_agc * alc_decay     * alc_var;  if ( alc_agc > limit_sup) { alc_agc =  limit_sup;}}
			else               { alc_agc -= alc_agc * coeff_attack  * alc_var;  if ( alc_agc < limit_inf) { alc_agc =  limit_inf;} ; if ( alc_agc < alc_agc_min )  alc_agc_min = alc_agc; }  // Prevent zero or "negative" gain values
												// 0.07                              // 0.001
												// 0.1
			adb.agc_valbuf [i] =  alc_agc;	    // store in "running" ALC history buffer for later application to audio data
		}

		ads.alc_val = alc_agc;

		// float32_t difff = (float32_t) alc_agc_min - ts.tx_alc_agc_min ;
		// if (difff < 0) { ts.tx_alc_agc_min += difff/2; }
		// alc_val_min     = AudioDriver_absmax ( buffer, blockSize );
        // arm_max_f32 ( buffer, blockSize, ts.tx_alc_agc_min, )

        if ( alc_agc_min < ts.tx_alc_agc_min )   ts.tx_alc_agc_min  = alc_agc_min;  /// pour afficher sur ecran la profondeur en cours du compresseur en temps reel

        // Delay the post-ALC audio slightly so that the ALC's "attack" will very slightly lead the audio being acted upon by the ALC.
        // This eliminates a "click" that can occur when a very strong signal appears due to the ALC lag.  The delay is adjusted based on
        // decimation rate so that it is constant for all settings.

        // Update the in/out pointers to the ALC delay buffer

  /*    cumulateur          /= blockSize;
        cumulateur_avg      += ( -cumulateur_avg + cumulateur ) * 0.5;
        float32_t   val_cmp  = current_signal ? cumulateur_avg : cumulateur ;

        if (  val_cmp < ts.Noise_Gate_Thresh ) 	{ current_signal = false;  memset ( buffer, 0 , (blockSize << 2)  ); }
        else                                    { current_signal = true;                                             }
   */

		// test avec bouton "Step Button Swap"  si le retard d'action donne un meilleur punch : resultat oui
	    // 	if (ts.freq_step_config & FREQ_STEP_SWAP_BTN)  // pour tester  si le retard d'action CAG est meilleur ou pas

        {
        // N96K B3 (audit 4.6): 320 is the 48 kHz constant: the AM and FM TX paths call this with
        // blockSize 32 and get a 288 sample (6.0 ms) look-ahead. SSB TX runs in the 12 kHz domain
        // (blockSize 8) and uses 176 -> 168 samples (14.0 ms) of look-ahead, Nizar's deliberate
        // tuning. With block aligned indices both windows fit AUDIO_DELAY_BUFSIZE=320 (max index 319).
        	uint16_t   buffer_boucle =  ts.tx_decimatedIQ ? 176 : 320 ;   // 96 : 320;  // multiple de 8 ou de 32

        // N96K B3: the ring length and the block size change with the TX path (12 kHz SSB 176/8 vs.
        // 48 kHz AM/FM 320/32). The persistent indices are only block aligned for the geometry they
        // were advanced with: after SSB->AM/FM the unaligned phase lets the write/read window reach
        // up to index 343, i.e. up to 24 floats past audio_delay_buffer[AUDIO_DELAY_BUFSIZE] (OOB
        // read/write in CCM). Re-align the ring whenever the geometry changes.
        if ( (buffer_boucle != alc_delay_ring_len) || (blockSize != alc_delay_block) )
        {
            alc_delay_inbuf    = 0;
            alc_delay_outbuf   = 0;
            alc_delay_ring_len = buffer_boucle;
            alc_delay_block    = blockSize;
        }
			alc_delay_inbuf         +=  blockSize;
			alc_delay_outbuf         =  alc_delay_inbuf + blockSize;
			alc_delay_inbuf         %=  buffer_boucle;    // 6.66ms circular Buffer delay de 320 cases a 48000 ks/s   // AUDIO_DELAY_BUFSIZE = (128/2)*5 = 320;
			alc_delay_outbuf        %=  buffer_boucle;    // 6.66ms circular Buffer delay de 320 cases a 48000 ks/s   // AUDIO_DELAY_BUFSIZE = (128/2)*5 = 320;

			arm_copy_f32 ( buffer                               , &audio_delay_buffer[alc_delay_inbuf], blockSize );  // put new data into the delay buffer
			arm_copy_f32 ( &audio_delay_buffer[alc_delay_outbuf], buffer                              , blockSize );  // take old data out of the delay buffer
		}



     //  arm_mult_f32 ( buffer, adb.agc_valbuf, buffer, blockSize );		// Apply ALC gain corrections to TX audio channels

     //  Niz_mult_f32 ( buffer, adb.agc_valbuf, buffer);

	 //	 if   ( modulation_scaling > 0.999) { Niz_mult_f32_saturation_35500 ( buffer, adb.agc_valbuf, buffer            ); }  // SSB (USB + LSB)
	 //	 else                               { arm_mult_f32                  ( buffer, adb.agc_valbuf, buffer, blockSize ); }

        				// Adoucire le tableau des coeffs de compression par filtrage passe bas 52Hz @-3db

       arm_iir_lattice_f32_Nizar ( &IIR_LPF_Compressor,  adb.agc_valbuf,  adb.agc_valbuf, blockSize );

       arm_mult_f32              ( buffer             ,  adb.agc_valbuf,  buffer,         blockSize );


      /*   if ( ts.show_debug_info )
       {
    	   Tx_Log_Compressor ( buffer, blockSize );  // compresseur logarithmique de la voix
    	   alc_final_output_level_gain *= 1.33333;
       }*/


       arm_scale_f32  ( buffer,  alc_final_output_level_gain,  buffer,  blockSize );
    }
}

//*****************************************************************************************************


/*
void  AudioDriver_I_PRE_DISTORSION_Add ( float32_t * pdp_buffer, const  uint16_t  blockSize  )   // 32 blockSize
{
	float32_t   alpha = ts.pdp_alpha;
	float32_t   betta = ts.pdp_betta;

	for ( uint16_t i = 0 ; i < blockSize; i += 4 )
	{

		//**************  1er ordre *********************

		float32_t   a    = pdp_buffer[i    ];
		float32_t   b    = pdp_buffer[i + 1];
		float32_t   c    = pdp_buffer[i + 2];
		float32_t   d    = pdp_buffer[i + 3];

		//************* 3em ordre  *********************
		float32_t   aa   =   a * a;
		float32_t   bb   =   b * b;
		float32_t   cc   =   c * c;
		float32_t   dd   =   d * d;
		float32_t  Ya    = aa  * alpha;
		float32_t  Yb    = bb  * alpha;
		float32_t  Yc    = cc  * alpha;
		float32_t  Yd    = dd  * alpha;
		float32_t  aaa   =   a * Ya;
		float32_t  bbb   =   b * Yb;
		float32_t  ccc   =   c * Yc;
		float32_t  ddd   =   d * Yd;

		//*************  5em ordre ********************

		float32_t  Za    = aa  *  betta;
		float32_t  Zb    = bb  *  betta;
		float32_t  Zc    = cc  *  betta;
		float32_t  Zd    = dd  *  betta;

				   Za    = Za  * aaa;
				   Zb    = Zb  * bbb;
				   Zc    = Zc  * ccc;
				   Zd    = Zd  * ddd;


	    //************** Addition finale  *****************
		pdp_buffer[i  ]  = a + aaa  + Za ;
		pdp_buffer[i+1]  = b + bbb  + Zb ;
		pdp_buffer[i+2]  = c + ccc  + Zc ;
		pdp_buffer[i+3]  = d + ddd  + Zd ;
	}
}
*/






/**
 * @brief Equalize based on band and simultaneously apply I/Q gain AND phase adjustments
 * The input is IQ data in the adb.i_buffer and adb.q_buffer
 * @param dst output buffer for generated IQ audio samples
 * @param blockSize number of samples (input/output)
 * @param swap i and q buffers meaning
 * @param scaling gain applied to the samples before conversion to integer data
 *
 */

static  void  AudioDriver_Tx_IQ_Processing_Final ( float32_t scaling, bool swap, AudioSample_t* const dst, const uint16_t  blockSize )
{
    int16_t    trans_idx;

    if ( ts.dmod_mode == DEMOD_CW || ts.iq_freq_mode == FREQ_IQ_CONV_MODE_OFF) {  trans_idx = IQ_TRANS_OFF; }
    else    																   {  trans_idx = IQ_TRANS_ON;  }

    float32_t   *final_i_buffer, *final_q_buffer;

    if ( ts.tx_decimatedIQ )       				// ts.tx_decimatedIQ = ( (USB + LSB) & not_DIG_IQ )
    {
    	if (( ts.tx_audio_source==TX_AUDIO_DIG_EX ) && !ts.tune ) { scaling *= ((float32_t) ts.tx_DIG_EX_gain) * 0.45; }  // amplification DIG-EX par un coeff du menu
    	else                                                      { scaling *= 1.667;                                  }
    }


    float32_t  final_i_gain = ts.tx_power_factor * ts.tx_adj_gain_var [ trans_idx ].i * scaling;  // coeff de correction IQ en Amplitude
    float32_t  final_q_gain = ts.tx_power_factor * ts.tx_adj_gain_var [ trans_idx ].q * scaling;

    				  //  I/Q data with or without swapping, putting it in "upper" or "Lower" sideband (above the LO)

    if(swap == false) {  final_i_buffer = adb.i_buffer;     final_q_buffer = adb.q_buffer;  }
    else	 	      {  final_i_buffer = adb.q_buffer;     final_q_buffer = adb.i_buffer;  }


		//*************************************************************************************************
        //************************************ TX  I/Q Final  Scaling ***************************************************
        //*************************************************************************************************

	  if ( ts.dmod_mode != DEMOD_CW )
	   {
		  arm_scale_f32_saturation_30000 ( final_i_buffer, final_i_gain, final_i_buffer, blockSize );
		  arm_scale_f32_saturation_30000 ( final_q_buffer, final_q_gain, final_q_buffer, blockSize );
	   }
	   else
	   {
		  arm_scale_f32 ( final_i_buffer, final_i_gain, final_i_buffer, blockSize );
		  arm_scale_f32 ( final_q_buffer, final_q_gain, final_q_buffer, blockSize );
	   }

	  // Ajouter de predistorsion pour reduire les IMD (Intermodulations)en sortie HF du PA pour une pureté spectrale.

	  //	  AudioDriver_I_PRE_DISTORSION_Add ( final_i_buffer, blockSize );





	  	  	  	  	  	  	  // this is the IQ phase adjustment  /// NIZZZ  on a avanc� la correction de phase sur l'amplitude
	  AudioDriver_IQPhaseAdjust ( ts.txrx_mode, final_i_buffer, final_q_buffer, blockSize );

	  //******************************************************************************************************
	  //*********************  Tx Filtre IIR anti-repliement pour Interpolation 48Khz --> 96Khz **************
	  //********************** Tx IIR  16.5 Khz  4em ordre  elleptique  **************************************
	  //******************************************************************************************************
	  uint16_t    BS_64 = 64;
	  float32_t  audio_96_buffer_I[64];
	  float32_t  audio_96_buffer_Q[64];

	  for ( uint16_t i=0; i < BS_64; i+=2u )
	  {
		  audio_96_buffer_I[ i  + 1u ] = audio_96_buffer_I[ i ] = final_i_buffer[ (i >> 1u)];
		  audio_96_buffer_Q[ i  + 1u ] = audio_96_buffer_Q[ i ] = final_q_buffer[ (i >> 1u)];
	  }

	  //******************************************************************************************************
	  //**** Tx Filtre LPF IIR 4em 16.30Khz pour  Interpolation   48Khz--->96Khz est le même utilisé en  ******
	  //******************************************************************************************************

	  arm_iir_lattice_f32_Nizar ( &IIR_AntiAlias_48_to_96_I, audio_96_buffer_I, audio_96_buffer_I, BS_64 );
	  arm_iir_lattice_f32_Nizar ( &IIR_AntiAlias_48_to_96_Q, audio_96_buffer_Q, audio_96_buffer_Q, BS_64 );

    for ( int i = 0; i < BS_64; i++ )	// Prepare data for DAC
    {
        dst[ i ].l = I2S_correctHalfWord ( audio_96_buffer_I [i] );  // save left channel
        dst[ i ].r = I2S_correctHalfWord ( audio_96_buffer_Q [i] );  // save right channel
    }
}



///*************************************************************************************************************
typedef struct
{
    __packed int16_t l;        // NIZZ int16_t
    __packed int16_t r;
}  AudioSample_t16;



static  void  AudioDriver_TxAudioBufferFill ( AudioSample_t * const src, const uint16_t blockSize )
{
    const    uint8_t   tx_audio_source  = ts.tx_audio_source;

    AudioSample_t16 *  srcaudio_USB     = (AudioSample_t16 *) src;

    float32_t   gain_calc;
    float32_t   post_filt_coef;

    if ( ts.tune )     												        // TUNE mode?  If so, generate tone so we can adjust TX IQ phase and gain
    {
       	softdds_runIQ ( adb.a_buffer[0], adb.a_buffer[0], 32 );             // load audio buffer with the tone - DDS produces quadrature channels, but we need only one

       	if (ts.tx_decimatedIQ) {  gain_calc      = ts.menu_mode ? 2.70 :  5.50; }  // depend si  Tune est (Dial) ou (Dial+750Hz)
       	else                   {  gain_calc      = 1.0;                         }
       	post_filt_coef = 1.0;
    }
    else
    {
        switch ( tx_audio_source )
        {
        	case TX_AUDIO_MIC     :	{ gain_calc = ts.tx_mic_gain_mult;  gain_calc /= 2;      } break;  // We are in MIC In mode:  Calculate Microphone gain
        	case TX_AUDIO_DIG_EX  :	{ gain_calc = 1;                                    	 } break;
			case TX_AUDIO_LINEIN_L:    				              // Are we in LINE IN mode?
			case TX_AUDIO_LINEIN_R: { gain_calc = 56;      /* 20  // LINE_IN_GAIN_RESCALE;*/ } break;  // Yes - fixed gain scaling for line input - the rest is done in hardware
			default:				{ gain_calc = 1;	                                     } break;
        }

        if ( tx_audio_source != TX_AUDIO_DIG_EX )
        {
        	gain_calc  *=  0.15258e-4;  // gain du � la version 32 bits au lieu de 16bit codec

        	 if (( tx_audio_source == TX_AUDIO_MIC ) && (ts.tx_comp_level == 1) ) // On rajoute du gain suplementaire pour le compresseur de la voix microphonique afin d'assurer un taux de compression
        	{
        		 post_filt_coef = ts.alc_tx_postfilt_gain_float ;
        		 gain_calc     *= ts.alc_tx_postfilt_gain_float ;  // pres_compressor gain if microphone voice only
         	}
        }

        float32_t   ii_bloc[64];

        							// Are we in LINE IN RIGHT CHANNEL mode?
        if      ( tx_audio_source == TX_AUDIO_LINEIN_R )  {  for ( int i = 0; i < blockSize; i++ )  { ii_bloc[i] = I2S_correctHalfWord ( src[i].r); } }  // audio buffer with right sample channel
        else if ( tx_audio_source == TX_AUDIO_DIG_EX   )  {  for ( int i = 0; i < blockSize; i++ )  { ii_bloc[i] =              srcaudio_USB[i].l ; } }  // USB audio DIG
        else                                              {  for ( int i = 0; i < blockSize; i++ )  { ii_bloc[i] = I2S_correctHalfWord ( src[i].l); } }  // audio buffer with left  sample channel


        //****************************************************************************************************
		//*************  Tx Filtrage microphone LPF IIR 4em  3.84Khz/-3db  avant decimation 96Khz--> 48Khz ******
		//****************************************************************************************************

		arm_iir_lattice_f32_Nizar ( &IIR_Tx_LPF_3K84_Filter,  ii_bloc,  ii_bloc,  blockSize  );

		//****************************************************************************************************
		//***************************   TX  Decimation 96Khz--> 48Khz    *************************************
		//****************************************************************************************************

        uint16_t   BS_32 = 32;
		for ( int32_t io = 0 ; io < BS_32; io++ )            // on va prendre un point sur deux
		{
			adb.a_buffer[0][ io ] = ii_bloc[ io << 1u ];
		}
    }
			//*************************************************************************************************
			//***** Tx  LPF 3.6Khz ( FIR 43taps) + Decimation 48Khz --> 12Khz ,  USB , LSB  *******************
			//*************************************************************************************************

    uint16_t   blockSize_tmp = 32;

	if ( ts.tx_decimatedIQ )			// ts.tx_decimatedIQ = ( (USB + LSB) & not_DIG_IQ )
	{
		arm_fir_decimate_f32_Nizar ( &DECIMATE_TX, adb.a_buffer[0], adb.a_buffer[0], blockSize_tmp );      // LPF 3.6Khz ( FIR 43taps) + decimation 1/4
		blockSize_tmp =  8u;    // blockSize/4;
	}

	if ( gain_calc != 1.0 )
	{
		arm_scale_f32 ( adb.a_buffer[0], gain_calc, adb.a_buffer[0], blockSize_tmp );    // apply gain
	}

	if ( ts.tx_meter_mode == METER_AUDIO )
	{
		ads.peak_audio = AudioDriver_absmax ( adb.a_buffer[0], blockSize_tmp );  // calcul Max pour affichage au "AUD" Meter
		if (( tx_audio_source == TX_AUDIO_MIC ) && (ts.tx_comp_level == 1) )   { ads.peak_audio /=  post_filt_coef; }
	}
}


//******************************************************************************************************************

/**
 * takes audio samples in adb.a_buffer[0] and produces SSB in adb.i_buffer/adb.q_buffer
 * audio samples should filtered before passed in here if necessary
 */

static  void  AudioDriver_TxProcessorModulatorSSB ( AudioSample_t * const dst, const uint16_t blockSize, const uint8_t iq_freq_mode, const bool is_lsb)
{
				// This is a phase-added 0-90 degree Hilbert transformer that also does low-pass and high-pass filtering
				// to the transmitted audio.  As noted above, it "clobbers" the low end, which is why we made up for it with the above filter.

    if ( ts.tx_decimatedIQ ) // si decimation Fs = 12Khz    ts.tx_decimatedIQ = ( (USB + LSB) & not_DIG_IQ )
    {

    	arm_fir_f32_Nizar ( &Fir_Tx_Hilbert_Q, adb.a_buffer[0], adb.a_buffer[1], 8 );	 // - 90 deg to Q data Hilbert
    	arm_fir_f32_Nizar ( &Fir_Tx_Hilbert_I, adb.a_buffer[0], adb.a_buffer[0], 8 );	 // +  0 deg to I data Hilbert

    	// Ajouter de predistorsion pour reduire les IMD (Intermodulations)en sortie HF du PA pour une pureté spectrale.

      //   AudioDriver_I_PRE_DISTORSION_Add ( adb.a_buffer[0],   8   );
      //   AudioDriver_I_PRE_DISTORSION_Add ( adb.a_buffer[1],   8   );
		         //*******************************************************************************************
    			 //******* Tx Interpolation  de 12Khz ---> 48Khz  ********************************************
    	         //*******************************************************************************************

	   arm_fir_interpolate_f32_Nizar ( &Tx_FIR_INTERPOLATE_12_to_48[0], adb.a_buffer[0], adb.i_buffer, 8 );
	   arm_fir_interpolate_f32_Nizar ( &Tx_FIR_INTERPOLATE_12_to_48[1], adb.a_buffer[1], adb.q_buffer, 8 );

    }
    else    // si pas de decimation : FS = 48Khz
    {
    	arm_fir_f32_Nizar ( &Fir_Tx_Hilbert_I, adb.a_buffer[0], adb.i_buffer, blockSize );	// +  0 deg to I data Hilbert
    	arm_fir_f32_Nizar ( &Fir_Tx_Hilbert_Q, adb.a_buffer[0], adb.q_buffer, blockSize );	// - 90 deg to Q data Hilbert
    }

    if ( iq_freq_mode )
    {
								// is transmit frequency conversion to be done?
								// LSB && (-6kHz || -12kHz) --> true, else false
								// USB && (+6kHz || +12kHz) --> true, else false
        bool swap = is_lsb == true && (iq_freq_mode == FREQ_IQ_CONV_M6KHZ || iq_freq_mode == FREQ_IQ_CONV_M12KHZ);
             swap = swap || ((is_lsb == false) && (iq_freq_mode == FREQ_IQ_CONV_P6KHZ || iq_freq_mode == FREQ_IQ_CONV_P12KHZ));

        AudioDriver_FreqConversion ( adb.i_buffer, adb.q_buffer, blockSize, swap );
    }

    			// apply I/Q amplitude & phase adjustments
    AudioDriver_Tx_IQ_Processing_Final (  74252, is_lsb, dst, blockSize );   // SSB_GAIN_COMP  // 1.133 * 65536  NIZ32

}

/***
 * takes the I Q input buffers containing the I Q audio for a single AM sideband and returns the frequency translated IQ sideband
 * in the i/q input buffers given as argument.
 */


static void AudioDriver_TxProcessorAMSideband ( float32_t* i_buffer, float32_t* q_buffer,  const int16_t blockSize )
{
    		// generate AM carrier by applying a "DC bias" to the audio

    arm_offset_f32 ( i_buffer, AM_CARRIER_LEVEL       , i_buffer, blockSize );
    arm_offset_f32 ( q_buffer, (-1 * AM_CARRIER_LEVEL), q_buffer, blockSize );

    		// check and apply correct translate mode

    AudioDriver_FreqConversion ( i_buffer, q_buffer, blockSize, (ts.iq_freq_mode == FREQ_IQ_CONV_P6KHZ || ts.iq_freq_mode == FREQ_IQ_CONV_P12KHZ));
}

//**********************************************************************************************************************************
//********************** By NIzar pour rajouter  distorsion d'ordre pairau mic , des harmoniques sans intermodulation impaire ******
//**********************************************************************************************************************************

/*
#define   COEFF_SQUARE   3E-6

void  Tx_Add_Square_Mic_Audio ( float32_t * pSrc,    float32_t * pDst )
{
  float32_t in1, in2, in3, in4, in5, in6, in7, in8;

    in1 = *pSrc;
    in2 = *(pSrc + 1);    in1 = in1 * in1;
    in3 = *(pSrc + 2);    in2 = in2 * in2; in1 *= COEFF_SQUARE;
    in4 = *(pSrc + 3);    in3 = in3 * in3; in2 *= COEFF_SQUARE;  *pDst       = in1;
    in5 = *(pSrc + 4);    in4 = in4 * in4; in3 *= COEFF_SQUARE;  *(pDst + 1) = in2;
    in6 = *(pSrc + 5);    in5 = in5 * in5; in4 *= COEFF_SQUARE;  *(pDst + 2) = in3;
	in7 = *(pSrc + 6);    in6 = in6 * in6; in5 *= COEFF_SQUARE;  *(pDst + 3) = in4;
	in8 = *(pSrc + 7);    in7 = in7 * in7; in6 *= COEFF_SQUARE;  *(pDst + 4) = in5;
    					  in8 = in8 * in8; in7 *= COEFF_SQUARE;  *(pDst + 5) = in6;
    					                   in8 *= COEFF_SQUARE;  *(pDst + 6) = in7;
    					                   	   	   	   	   	   	 *(pDst + 7) = in8;
}


*/


//*********************************************************************************************************



static   void  AudioDriver_Tx_Filter_Audio ( bool do_bandpass, bool pass_tout, bool do_bass_treble, float32_t* In_Block, float32_t* Out_Block, const uint16_t blockSize )
{
    if ( do_bandpass )
    {
        arm_iir_lattice_f32_Nizar ( &IIR_TX_LPF_Filter,  In_Block,  Out_Block, blockSize );   // Filtre IIR lattice ARMA IIR LPF 0-2.80 Khz Elleptic

        arm_iir_lattice_f32_Nizar ( &IIR_HPF_TXFilter , Out_Block,  Out_Block, blockSize );   // Filtre passe haut 75Hz pout  Tx 48kH=FC
    }


    if ( pass_tout )
    {
    	//if ( ts.show_debug_info )  // 2em filtre passe tout a 182Hz pour mieux compresser les transitoires microphoniques
		//{
			 arm_iir_lattice_f32_Nizar ( &IIR_TX_PASS_TOUT_Filter_2,  Out_Block,  Out_Block, blockSize );   // Filtre passe tout microphone 182Hz
		//}

    	arm_iir_lattice_f32_Nizar ( &IIR_TX_PASS_TOUT_Filter, Out_Block,  Out_Block, blockSize );    // Filtre passe tout microphone 137Hz
    }



    if ( do_bass_treble )  // biquad filter for bass & treble --> NOT enabled when using USB Audio (eg. for Digimodes)
    {
    	if ( !ts.dsp_inhibit )
    	{
    		arm_biquad_cascade_df1_f32_Nizar ( &IIR_TX_biquad, Out_Block, Out_Block, blockSize ); // a rajouter _Nizar
    	}
    }
}

//********************************************************************************************************************


static  void  AudioDriver_TxProcessorFM ( AudioSample_t * const src, AudioSample_t * const dst, uint16_t blockSize)
{
    static float32_t    hpf_prev_a, hpf_prev_b;
           float32_t    a, b;
    const  uint32_t     iq_freq_mode = ts.iq_freq_mode;

    static  uint32_t    fm_mod_idx = 0, fm_mod_accum = 0, fm_tone_idx = 0, fm_tone_accum = 0, fm_tone_burst_idx = 0, fm_tone_burst_accum = 0;

    float32_t    fm_mod_mult;

    // Fill I and Q buffers with left channel(same as right)
    //
    if ( RadioManagement_FmDevIs5khz() )   // are we in 5 kHz modulation mode?
    									{  fm_mod_mult = 2; }  // yes - multiply all modulation factors by 2
    else  							    {  fm_mod_mult = 1; }  // not in 5 kHz mode - used default (2.5 kHz) modulation factors

    AudioDriver_TxAudioBufferFill ( src, blockSize );

    blockSize = (blockSize >> 1u);    // NIZ96

    if (!ts.tune)
    {
        AudioDriver_Tx_Filter_Audio ( true, false, ts.tx_audio_source != TX_AUDIO_DIG_EX, adb.a_buffer[0], adb.a_buffer[0], blockSize );
    }

    if ( ts.tx_audio_source == TX_AUDIO_MIC )  AudioDriver_TxCompressor ( adb.a_buffer[0], blockSize, FM_ALC_GAIN_CORRECTION );  // Do the TX ALC and speech compression/processing

    // Do differentiating high-pass filter to provide 6dB/octave pre-emphasis - which also removes any DC component!  Takes audio from "a" and puts it into "a".
    for( int i = 0; i < blockSize; i++ )
    {
        a                  = adb.a_buffer[0][i];
        b                  = FM_TX_HPF_ALPHA * ( hpf_prev_b + a - hpf_prev_a );    // do differentiation
        hpf_prev_a         = a;     // save "[n-1] samples for next iteration
        hpf_prev_b         = b;
        adb.a_buffer[0][i] = b;    // save differentiated data in audio buffer
    }

    // do tone generation using the NCO (a.k.a. DDS) method.  This is used for subaudible tone generation and, if necessary, summing the result in "a".
    if ( ( ads.fm_subaudible_tone_word ) && ( !ads.fm_tone_burst_active ) )    // generate tone only if it is enabled (and not during a tone burst)
    {
        for(int i = 0; i < blockSize; i++)
        {
            fm_tone_accum      +=  ads.fm_subaudible_tone_word;   // generate tone using frequency word, calculating next sample
            fm_tone_accum      &=  0xffffff;              // limit to 16 Meg range
            fm_tone_idx         =  ( fm_tone_accum >> FM_TONE_DDS_ACC_SHIFT);    // shift accumulator to index sine table
            fm_tone_idx        &=  (DDS_TBL_SIZE-1);        // limit lookup to range of sine table
            adb.a_buffer[0][i] +=  ( (float32_t) (DDS_TABLE[fm_tone_idx]) * FM_TONE_AMPLITUDE_SCALING * fm_mod_mult ); // load indexed sine wave value, adding it to audio
        }
    }

    // do tone  generation using the NCO (a.k.a. DDS) method.  This is used for tone burst ("whistle-up") generation, summing the result in "a".
    if  ( ads.fm_tone_burst_active )                // generate tone burst only if it is enabled
    {
        for ( int i = 0; i < blockSize; i++ )
        {
            // Calculate next sample
            fm_tone_burst_accum +=  ads.fm_tone_burst_word;  // generate tone using frequency word, calculating next sample
            fm_tone_burst_accum &=  0xffffff;                // limit to 16 Meg range
            fm_tone_burst_idx    =  (fm_tone_burst_accum >> FM_TONE_DDS_ACC_SHIFT);    // shift accumulator to index sine table
            fm_tone_burst_idx   &=  (DDS_TBL_SIZE-1);      // limit lookup to range of sine table
            adb.a_buffer[0][i]  +=  ( (float32_t) ((DDS_TABLE[fm_tone_burst_idx]) * FM_MOD_SCALING * fm_mod_mult) / FM_TONE_BURST_MOD_SCALING);    // load indexed sine wave value, adding it to audio
        }
    }
    //
    // do audio frequency modulation using the NCO (a.k.a. DDS) method, carrier at 6 kHz.  Audio is in "a", the result being quadrature FM in "i" and "q".
    //
    uint32_t  fm_freq_mod_word = FM_FREQ_MOD_WORD *  ((iq_freq_mode == FREQ_IQ_CONV_P12KHZ || iq_freq_mode == FREQ_IQ_CONV_M12KHZ)?2:1);
    // in case of 12Khz offset from base we need to adjust the fm_freq_mod_word

    for ( int i = 0; i < blockSize; i++ )
    {
        // Calculate next sample
        fm_mod_accum   +=  (ulong)(fm_freq_mod_word + (adb.a_buffer[0][i] * FM_MOD_SCALING * fm_mod_mult));   // change frequency using scaled audio
        fm_mod_accum   &=  0xffff;             // limit to 64k range
        fm_mod_idx      =  (fm_mod_accum >> FM_MOD_DDS_ACC_SHIFT);
        fm_mod_idx     &=  ( DDS_TBL_SIZE - 1);       // limit lookup to range of sine table
        adb.i_buffer[i] =  (float32_t)(DDS_TABLE[fm_mod_idx]);               // Load I value
        fm_mod_idx     +=  ( DDS_TBL_SIZE/4); // do 90 degree shift by indexing 1/4 into sine table
        fm_mod_idx     &=  ( DDS_TBL_SIZE - 1);       // limit lookup to range of sine table
        adb.q_buffer[i] =  (float32_t)( DDS_TABLE[fm_mod_idx]);   // Load Q value
    }

    bool swap = (iq_freq_mode == FREQ_IQ_CONV_P6KHZ || iq_freq_mode == FREQ_IQ_CONV_P12KHZ);

    AudioDriver_Tx_IQ_Processing_Final ( 57344 , swap, dst, blockSize );  // FM_MOD_AMPLITUDE_SCALING   // 0.875 * 65536  NIZ32
}

  #ifdef USE_FREEDV
// DO NOT USE, HAS NOT BEEN KEPT UP TO DATE WITH DEVELOPMENT IN audio_tx_processor!
//
// This is a stripped-down TX processor - work in progress
//
//*----------------------------------------------------------------------------
//* Function Name       : audio_dv_tx_processor
//* Object              :
//* Object              : audio sample processor for DV modes - USB only, that must be reconfigured to operate at 8ksps -  - but this will require the future addition of a circular buffer and queueing in ISR!
//* Input Parameters    :
//* Output Parameters   :
//* Functions called    :
//*----------------------------------------------------------------------------



static void AudioDriver_TxProcessorDigital ( AudioSample_t * const src, AudioSample_t * const dst, int16_t  blockSize )
{
    // Freedv Test DL2FW
    static int16_t         outbuff_count     = 0;
    static int16_t         trans_count_in    = 0;
    static int16_t         FDV_TX_fill_in_pt = 0;
    static FDV_IQ_Buffer*  out_buffer        = NULL;
    static int16_t         modulus_NF        = 0, modulus_MOD = 0;

    // If source is digital usb in, pull from USB buffer, discard line or mic audio and
    // let the normal processing happen

    if (ts.digital_mode == DigitalMode_FreeDV)
    { //we are in freedv-mode

        AudioDriver_TxAudioBufferFill ( src, blockSize );

        blockSize = ( blockSize >> 1u ) ; // NIZ96 apres decimation 96Khz --> 48Khz
        // *****************************   DV Modulator goes here - ads.a_buffer must be at 8 ksps

        // Freedv Test DL2FW

        // we have to add a decimation filter here BEFORE we decimate
        // for decimation-by-6 the stopband frequency is 48/6*2 = 4kHz
        // but our audio is at most 3kHz wide, so we should use 3k or 2k9


        // this is the correct DECIMATION FILTER (before the downsampling takes place):
        // use it ALWAYS, also with TUNE tone!!!
        AudioDriver_Tx_Filter_Audio ( true, false, false,  adb.a_buffer[0], adb.a_buffer[0], blockSize );


        // DOWNSAMPLING
        for (int k = 0; k < blockSize; k++)
        {
            if (k % 6 == modulus_NF)  //every 6th sample has to be catched -> downsampling by 6
            {
                fdv_audio_buff[FDV_TX_fill_in_pt].samples[trans_count_in] = ((int32_t)adb.a_buffer[0][k])/4;
                // FDV_TX_in_buff[FDV_TX_fill_in_pt].samples[trans_count_in] = 0; // transmit "silence"
                trans_count_in++;
            }
        }

        modulus_NF += 4u; //  shift modulus to not loose any data while overlapping
        modulus_NF %= 6;//  reset modulus to 0 at modulus = 12

        if (trans_count_in == FDV_BUFFER_SIZE) //yes, we really hit exactly 320 - don't worry
        {
            //we have enough samples ready to start the FreeDV encoding

            fdv_audio_buffer_add( &fdv_audio_buff [ FDV_TX_fill_in_pt ] );
            //handshake to external function in ui.driver_thread
            trans_count_in = 0;

            FDV_TX_fill_in_pt++;
            FDV_TX_fill_in_pt %= FDV_BUFFER_AUDIO_NUM;
        }

        if (out_buffer == NULL && fdv_iq_has_data() > 1) {  fdv_iq_buffer_remove( &out_buffer ); }

        if (out_buffer != NULL) // freeDV encode has finished (running in ui_driver.c)?
        {

            // Best thing here would be to use the arm_fir_decimate function! Why?
            // --> we need phase linear filters, because we have to filter I & Q and preserve their phase relationship
            // IIR filters are power saving, but they do not care about phase, so useless at this point
            // FIR filters are phase linear, but need processor power
            // so we now use the decimation function that upsamples like the code below, BUT at the same time filters
            // (and the routine knows that it does not have to multiply with 0 while filtering: if we do upsampling and subsequent
            // filtering, the filter does not know that and multiplies with zero 5 out of six times --> very inefficient)
            // BUT: we cannot use the ARM function, because decimation factor (6) has to be an integer divide of
            // block size (which is 64 in our case --> 64 / 6 = non-integer!)

            // UPSAMPLING [by hand]
            for ( int j = 0;  j < blockSize; j++ ) //  now we are doing upsampling by 6
            {
                if ( modulus_MOD == 0 ) // put in sample pair
                {
                    adb.i_buffer[j] = out_buffer->samples[outbuff_count].real; // + (sample_delta.real * (float32_t)modulus_MOD);
                    adb.q_buffer[j] = out_buffer->samples[outbuff_count].imag; // + (sample_delta.imag * (float32_t)modulus_MOD);
                }
                else // in 5 of 6 cases just stuff in zeros = zero-padding / zero-stuffing
                {
                    adb.i_buffer[j] = 0;
                    adb.q_buffer[j] = 0;

                }
                modulus_MOD++;
                if ( modulus_MOD == 6 )
                {
                    // last_sample.real = FDV_TX_out_buff[modem_buffer_offset].samples[outbuff_count].real;
                    // last_sample.imag = FDV_TX_out_buff[modem_buffer_offset].samples[outbuff_count].imag;

                    outbuff_count++;
                    modulus_MOD = 0;
                }
            }

            // Add interpolation filter here to suppress alias frequencies
            // we are upsampling from 8kHz to 48kHz, so we have to suppress all frequencies below 4kHz
            // our FreeDV signal is centred at 1500Hz ??? and is 1250Hz broad,
            // so a lowpass filter with cutoff frequency 2400Hz should be fine!

            // INTERPOLATION FILTER [after the interpolation has taken place]
            // the samples are now in adb.i_buffer and adb.q_buffer, so lets filter them

            arm_fir_f32_Nizar ( &Fir_TxFreeDV_Interpolate_I,  adb.i_buffer, adb.i_buffer,  blockSize );

            arm_fir_f32_Nizar ( &Fir_TxFreeDV_Interpolate_Q,  adb.q_buffer, adb.q_buffer,  blockSize );
        }
        else
        {
            profileEvent ( FreeDVTXUnderrun );
            // memset(dst,0,blockSize*sizeof(*dst));
        }

        if (outbuff_count >= FDV_BUFFER_SIZE)
        {
            outbuff_count = 0;
            out_buffer    = NULL;
            fdv_iq_buffer_remove(&out_buffer);
        }

        // apply I/Q amplitude & phase adjustments
        bool swap = ts.dmod_mode == DEMOD_USB || (ts.dmod_mode == DEMOD_DIGI && ts.digi_lsb == false);
        AudioDriver_Tx_IQ_Processing_Final ( 1485045 , swap, dst, blockSize );  // 20.0*SSB_GAIN_COMP  // 20 * 74252   NIZ32
    }
    else
    {
        memset ( dst, 0, blockSize*sizeof( *dst ) );    // Pause or inactivity
    }
}
#endif

//***************************************************************************************************



static  void  AudioDriver_TxProcessorRtty ( AudioSample_t * const dst, uint16_t  blockSize )
{

	for ( uint16_t idx =0; idx < blockSize; idx++ )
	{
		adb.a_buffer[0][idx] = Rtty_Modulator_GenSample();
	}

    AudioDriver_Tx_Filter_Audio           ( true, false, false, adb.a_buffer[0], adb.a_buffer[0], blockSize );
    AudioDriver_TxProcessorModulatorSSB ( dst, blockSize, ts.iq_freq_mode, ts.digi_lsb );

    // remove noise if no CW is keyed

    memset ( adb.a_buffer[0], 0, sizeof(adb.a_buffer[0][0])*blockSize);

    if (ts.cw_keyer_mode != CW_KEYER_MODE_STRAIGHT)
    {
    	CwGen_Process( adb.a_buffer[0], adb.a_buffer[0], blockSize );

    	// we just misuse adb.a_buffer[0], and generate a CW side tone
    }

}


///************************************************************************************************

static  void  AudioDriver_TxProcessorPsk ( AudioSample_t * const dst,  uint16_t  blockSize )
{

	for ( uint16_t idx =0;  idx < blockSize;  idx++)
	{
		adb.a_buffer[0][idx] = Psk_Modulator_GenSample();
	}

    AudioDriver_Tx_Filter_Audio           ( true, false, false, adb.a_buffer[0], adb.a_buffer[0], blockSize );
	AudioDriver_TxProcessorModulatorSSB ( dst, blockSize, false, false );


/*
    memset(adb.a_buffer[0],0,sizeof(adb.a_buffer[0])*blockSize);

    if (ts.cw_keyer_mode != CW_KEYER_MODE_STRAIGHT)
    {
    	CwGen_Process(adb.a_buffer[0], adb.a_buffer[0], blockSize);
    	// we just misuse adb.a_buffer[0], and generate a CW side tone
    }
*/


}

  float32_t     Buffer_delay[ 121 ][ 8 ];
  uint8_t       indice_delay_stockage = 120;
  uint8_t       indice_delay_lecture  = 0;

 void  AudioDriver_Tx_Buffer_Delay_RAZ ()
{
    arm_fill_f32 ( 0, (float32_t*) Buffer_delay[ 0 ], 968);
}
///*************************************************************************************************************************
///********************************************  TX-Processing   ***********************************************************
//**************************************************************************************************************************

static void  AudioDriver_TxProcessor ( AudioSample_t * const src_Codec, AudioSample_t * const dst_Codec)  //  AudioSample_t * const audioDst,
{
							// we copy volatile variables which are used multiple times to local consts to let the compiler do its optimization magic
							// since we are in an interrupt, no one will change these anyway
							// shaved off a few bytes of code

     const    uint8_t       dmod_mode       = ts.dmod_mode;
     const    uint8_t       tx_audio_source = ts.tx_audio_source;
     const    uint8_t       tune            = ts.tune;
     const    uint8_t       iq_freq_mode    = ts.iq_freq_mode;
              uint16_t      blockSize_64    =  64;
              uint16_t      blockSize_32    =  32;
              uint16_t             BS_8     =  8;

    AudioSample_t16         srcUSB [ 64 ];  // 16 bit stereo samples from the USB audio interface
    // [build fix, GCC >= 14] srcUSB deliberately holds 16 bit stereo pairs (audio_out_fill_tx_buffer()
    // below fills blockSize*2 int16_t values == sizeof(AudioSample_t16[64])), while the DSP pipeline
    // uses AudioSample_t.  This pointer is only handed over to AudioDriver_TxAudioBufferFill() /
    // AudioDriver_TxProcessorFM() / AudioDriver_TxProcessorDigital(), which cast it back to
    // AudioSample_t16* before touching the data - so only the address is propagated, an explicit
    // cast is required and not a single byte changes.
    AudioSample_t  * const  src             = ((tx_audio_source == TX_AUDIO_DIG_EX) || (tx_audio_source == TX_AUDIO_DIG_IQ)) ? (AudioSample_t *) srcUSB : src_Codec;

    						// if we want to know if our signal will go out, look at this flag
    bool    external_tx_mute = ts.audio_dac_output_muting_flag || (ts.audio_dac_output_muting_buffer_count > 0) ;
    bool    signal_active    = false;      // unless this is set to true, zero output will be generated
    bool    cw_signal_active = false;



    	//*********************************************************************************************************
    	//****************************** TX  Cable-USB  DIG + DIG-IQ   ************************************************
        //*********************************************************************************************************

						// If source is digital usb in, pull from USB buffer, discard line or mic audio and
						// let the normal processing happen

    if ( tx_audio_source == TX_AUDIO_DIG_EX || tx_audio_source == TX_AUDIO_DIG_IQ )   // Saisie des donn�es du cable USB vers un buffer scrUSB
    {
         audio_out_fill_tx_buffer ( (int16_t*) srcUSB, 64 * 2 );  // FIXME: change type of audio_out_fill_tx_buffer to use audio sample struct
    }

    if ( tx_audio_source == TX_AUDIO_DIG_IQ && dmod_mode != DEMOD_CW && !tune && !is_demod_psk() )
    {
						// If in CW mode or Tune  DIQ audio input is ignored
						// Output I and Q as stereo, fill buffer

    				    // Decimation par deux dans le buffer de traitement classique de 96Khz --> 48Khz
        for ( int i = 0; i < blockSize_32; i++ )	 			  // Copy to single buffer
        {
            adb.i_buffer[i] = srcUSB [i << 1u].l  ;  //  src[i].l;  // NIZ32
            adb.q_buffer[i] = srcUSB [i << 1u].r  ;  //  src[i].r;
        }

				   // sortie du signal venant du PC tel qu'il est vers le Codec sans traitement
				   // juste un scaling , correction I/Q Phase/Amplit, Saturation et Extrapol vers codec 96Khz
                   // rarement utilisé et il faut mieu l'aiguiller vers microphone ou vers manipulateur CW
                   // ceci tout en affichant le spectre Rx large sur un grand ecran d'un ordinateur

        AudioDriver_Tx_IQ_Processing_Final ( 65000 , false, dst_Codec, blockSize_32 );  //65536.0  // NIZ32
        signal_active = true;
    }
    			//****************************************************************************************************
    			//**************************  TX  Free-DV  ***************************************************************
    			//****************************************************************************************************
    else if (ts.dvmode == true)
    {
    	switch(ts.digital_mode)
    	{
									#ifdef USE_FREEDV
    		case DigitalMode_FreeDV: {	AudioDriver_TxProcessorDigital ( src, dst_Codec, blockSize_64 );	 signal_active = true;	}	break;
									#endif
    		case DigitalMode_RTTY  : {	AudioDriver_TxProcessorRtty    (      dst_Codec, blockSize_32 );	 signal_active = true;	}	break;
    		case DigitalMode_BPSK  : {	AudioDriver_TxProcessorPsk     (      dst_Codec, blockSize_32 );	 signal_active = true;	}	break;
		}
    }
      ///********************************************************************************************************
      ///**************************************  TX   CW  CW  CW ****************************************************
      ///********************************************************************************************************
    else if ( dmod_mode == DEMOD_CW || ts.cw_text_entry )
    {
        if (tune)
        {
            softdds_runIQ ( adb.i_buffer,  adb.q_buffer,  blockSize_32  );      // generate tone/modulation for TUNE
            				// Equalize based on band and simultaneously apply I/Q gain & phase adjustments
            signal_active = true;
        }
        else
        {
        	cw_signal_active = CwGen_Process (  adb.i_buffer,  adb.q_buffer,  blockSize_32 );

            if (external_tx_mute == false)
            {
            	signal_active = cw_signal_active;
            }
        }

        if ( signal_active )   // s'il n'y a pas de passage nul du CW , paddle inactif
        {
						// apply I/Q amplitude & p            djustments
						// Wouldn't it be necessar            nclude IF conversion here? DD4WH June 16th, 2016
						// Answer: NO, in CW that             e be changing the Si570 frequency during TX/RX switching . . .
            AudioDriver_Tx_IQ_Processing_Final ( 46000 /*65536*/ , ts.cw_lsb == 0, dst_Codec, blockSize_32 );   // NIZ32
        }
    }
    			///****************************************************************************************
    			///********************************** TX  SSB SSB SSB ****************************************
    			///****************************************************************************************

    else if ( is_ssb ( dmod_mode ) )		   // SSB processor
    {
        if (ads.tx_filter_adjusting == 0)  // s'il n'y a pas de modification des parametres des filtres tx en cours
        {
            AudioDriver_TxAudioBufferFill ( src, blockSize_64 );  // Saisie + Decimation de 96Khz --> 12Khz

            if ( ( ts.tx_audio_source == TX_AUDIO_MIC ) && ! ts.tune )
            {
				AudioDriver_Tx_Filter_Audio ( true, true, true , adb.a_buffer[0], adb.a_buffer[0], BS_8 );  // IIR 2.8K LPF + Tx parametric Equaliseur Basse_Treble

			/*	if ( ts.show_debug_info )     // filtre rejecteur en Tx a 1200Hz  bande passante 65Hz
				{
					arm_iir_lattice_f32_Nizar ( &IIR_TX_Voice_Notch_1200HZ, adb.a_buffer[0], adb.a_buffer[0], BS_8 );
				}*/


				if ( ts.reverberation_enable & 0x01)  AudioDriver_Tx_Reverberation ( adb.a_buffer[0] ); //}


			//	if (!ts.show_debug_info) { if ( ts.Noise_Gate_Thresh )  AudioDriver_Tx_Noise_Gate_Process (adb.a_buffer[0]);}

			/*	if (( ts.dsp_inhibit == 0 )   && ( ts.show_debug_info ) )
				{
					AudioDriver_RxProcessorNoiseReduction ( 8 , adb.a_buffer[0] );
				} */

				AudioDriver_TxCompressor  ( adb.a_buffer[0],  BS_8 , 1.0  );      // Do the TX ALC and speech compression/processing

				arm_copy_f32 ( adb.a_buffer[0],  Buffer_delay[indice_delay_stockage],  BS_8  ) ;
				arm_copy_f32 ( Buffer_delay[indice_delay_lecture],  adb.a_buffer[0],   BS_8  ) ;

				indice_delay_stockage++; if (indice_delay_stockage == 121) indice_delay_stockage = 0;
				indice_delay_lecture++;  if (indice_delay_lecture  == 121) indice_delay_lecture  = 0;

				// if ( !ts.show_debug_info)  { if ( ts.reverberation_enable  & 0x01) AudioDriver_Tx_Reverberation ( adb.a_buffer[0] ); }
            }

            AudioDriver_TxProcessorModulatorSSB ( dst_Codec,  blockSize_32  , iq_freq_mode, dmod_mode == DEMOD_LSB );
            signal_active = true;
        }
    }
    //**************************************************************************************
    //*******************************  TX   AM AM AM AM *****************************************
    //**************************************************************************************
										// AM handler - Generate USB and LSB AM signals and combine  [KA7OEI]
										//
    else if( dmod_mode == DEMOD_AM )	//	Is it in AM mode *AND* is frequency translation active?
    {
        if( iq_freq_mode && ( ads.tx_filter_adjusting == 0 ) )	 				// is translation active?
        {
            AudioDriver_TxAudioBufferFill ( src, blockSize_64 );

            //
            // Apply the TX equalization filtering:  This "flattens" the audio
            // prior to being applied to the Hilbert transformer as well as added low-pass filtering.
            // It does this by applying a "peak" to the bottom end to compensate for the roll-off caused by the Hilbert
            // and then a gradual roll-off toward the high end.  The net result is a very flat (to better than 1dB) response
            // over the 275-2500 Hz range.
            //



            if ( !tune )
            {
                // AudioDriver_Tx_Filter_Audio ( (ts.flags1 & FLAGS1_AM_TX_FILTER_DISABLE) == false, false, tx_audio_source != TX_AUDIO_DIG_EX, adb.a_buffer[0], adb.a_buffer[0], blockSize_32 );

            	AudioDriver_Tx_Filter_Audio (  false, false, tx_audio_source != TX_AUDIO_DIG_EX, adb.a_buffer[0], adb.a_buffer[0], blockSize_32 );
            	if ( ts.tx_audio_source == TX_AUDIO_MIC )  AudioDriver_TxCompressor ( adb.a_buffer[0], blockSize_32 , AM_ALC_GAIN_CORRECTION );    // Do the TX ALC and speech compression/processing
            }

				//
				// This is a phase-added 0-90 degree Hilbert transformer that also does low-pass and high-pass filtering
				// to the transmitted audio.  As noted above, it "clobbers" the low end, which is why we made up for it with the above filter.
				// Apply transformation AND audio filtering to buffer data
				//
				// + 0 deg to I data
				// AudioDriver_delay_f32((arm_fir_instance_f32 *)&FIR_I_TX,(float32_t *)(adb.a_buffer[0]),(float32_t *)(adb.i_buffer),blockSize);



            arm_fir_f32_Nizar ( &Fir_Tx_Hilbert_I, adb.a_buffer[0], adb.i_buffer, blockSize_32 );  //   0  deg to I data  FS=48Khz

            arm_fir_f32_Nizar ( &Fir_Tx_Hilbert_Q, adb.a_buffer[0], adb.q_buffer, blockSize_32 );  // - 90 deg to Q data  FS=48Khz

				// COMMENT:  It would be trivial to add the option of generating AM with just a single (Upper or Lower) sideband since we are generating the two, separately anyway
				// and putting them back together!  [KA7OEI]
				//
				//
				// First, generate the LOWER sideband of the AM signal

				// temporary buffers;

            float32_t  i2_buffer [ blockSize_32 ];
            float32_t  q2_buffer [ blockSize_32 ];


            arm_negate_f32 ( adb.i_buffer, q2_buffer, blockSize_32 ); // this becomes the q buffer for the upper  sideband
            arm_negate_f32 ( adb.q_buffer, i2_buffer, blockSize_32 ); // this becomes the i buffer for the upper  sideband


            AudioDriver_TxProcessorAMSideband ( adb.i_buffer,  adb.q_buffer,  blockSize_32 ); // now generate USB AM sideband signal // i/q now contain the LSB AM signal
            AudioDriver_TxProcessorAMSideband (    i2_buffer,     q2_buffer,  blockSize_32 ); // now generate USB AM sideband signal

            arm_add_f32 ( i2_buffer, adb.i_buffer, adb.i_buffer, blockSize_32 );
            arm_add_f32 ( q2_buffer, adb.q_buffer, adb.q_buffer, blockSize_32 );

            AudioDriver_Tx_IQ_Processing_Final ( 74252, false, dst_Codec, blockSize_32 );  // AM_GAIN_COMP  // 1.133 * 65536  NIZ32

            signal_active = true;
        }
    }

    					///***************************************************************************
    					///*********************** TX   FM  FM   *************************************
    					//*****************************************************************************

    else if ( dmod_mode == DEMOD_FM )	 	//	Is it in FM mode
    {
        //  *AND* is frequency translation active ( No FM possible unless in frequency translate mode! )

        if ( iq_freq_mode )
        {
            // FM handler  [KA7OEI October, 2015]

            AudioDriver_TxProcessorFM ( src, dst_Codec, blockSize_64 );

            signal_active = true;
        }
    }

    ///*********************************************************************************************************************
    ///******************************    TX  Output Mute  or CW Space   ****************************************************
    /// ********************************************************************************************************************

    if ( signal_active == false  || external_tx_mute )
    {
        arm_fill_f32 ( 0, (float32_t*) dst_Codec,  128 ); // size_128

        if ( ts.audio_dac_output_muting_buffer_count ) { ts.audio_dac_output_muting_buffer_count--; }		// Pause or inactivity
    }

   //*****************************************************************************************************************
   //*****************************************************************************************************************
   //*****************************************************************************************************************
}

/*
static void AudioDriver_IqFillSilence ( int32_t *s, uint16_t size)
{
    memset(s,0,size*sizeof(*s));
}*/

//*----------------------------------------------------------------------------
//* Function Name       : I2S_RX_CallBack
//* Object              :
//* Object              : audio sample processor
//* Input Parameters    :
//* Output Parameters   :
//* Functions called    :
//*----------------------------------------------------------------------------




void  AudioDriver_I2SCallback ( AudioSample_t *src,  AudioSample_t *dst  )

{
    static bool   to_rx     =  true;	// used as a flag to clear the RX buffer
    static bool   to_tx     =  true;	// used as a flag to clear the TX buffer
    static ulong  tcount    =  0;


        // c est tres bizarre, un bug de blocage de la radio des qu on initilise a 64 directement cette variable


        if (ts.txrx_mode == TRX_MODE_RX)
        {
            if ( (to_rx) || ( ts.audio_processor_input_mute_counter > 0u ) )	 	// the first time back to RX, clear the buffers to reduce the "crash"
            {
               	arm_fill_f32 ( 0, (float32_t*) src, 128 );     // memset ( src,  0,  512 );

                if ( to_rx )
                {
                    AudioDriver_ClearAudioDelayBuffer();  // clear AGC buffer
                    UhsdrHwI2s_Codec_ClearTxDmaBuffer();  // clear DMA buffer

                    ts.dsp_inhibit =  1;  // Inhiber le NR + Notch pour commuter tx->Rx en silence , ouverture avec augmentation du volume
                    to_rx          = false;
                    to_tx          = true;
                }
                if ( ts.audio_processor_input_mute_counter )  { ts.audio_processor_input_mute_counter--;  }
                                    // caused by the content of the buffers from TX - used on return from SSB TX


                					// muted input should not modify the ALC so we simply restore it after processing

                ts.dsp_inhibit++;    //  Inhiber le NR + Notch
                AudioDriver_RxProcessor( (AudioSample_t*) src,  (AudioSample_t*) dst );
                if ( ts.dsp_inhibit )  { ts.dsp_inhibit--;}
            }
            else
            {
                AudioDriver_RxProcessor (  (AudioSample_t*) src, (AudioSample_t*) dst );

                if ( ts.cw_keyer_mode != CW_KEYER_MODE_STRAIGHT )    //  A ou B
                {
                	if ( ts.cw_text_entry || ts.dmod_mode == DEMOD_CW) // FIXME to call always when straight mode reworked
            		{
						CwGen_Process( adb.i_buffer,  adb.q_buffer, 32 );
					}
                }
            }

            		// Set flag to indicate that we WERE receiving when we go back to transmit mode
        } // End of RX Mode Thread

        else  			// Transmit mode Tx
        {
            if ( to_tx || ( ts.audio_processor_input_mute_counter >  0) )	 	// the first time back to TX, or TX audio muting timer still active - clear the buffers to reduce the "crash"
            {
            	arm_fill_f32 ( 0, (float32_t*) src, 128);

                to_tx   =   false;      // caused by the content of the buffers from TX - used on return from SSB TX
                to_rx   =   true;		// Set flag to indicate that we WERE transmitting when we eventually go back to receive mode
                if ( ts.audio_processor_input_mute_counter )  {  ts.audio_processor_input_mute_counter--;  }

                									// muted input should not modify the ALC so we simply restore it after processing
                float32_t  alc_holder     = ads.alc_val;       // sauvegarder la derniere valeur du gain compresseur
                ts.dsp_inhibit++;                              // pour eviter instabilite des Quads de l'equaliseur Tx
                AudioDriver_TxProcessor ( (AudioSample_t*) src, (AudioSample_t*) dst );
                if (ts.dsp_inhibit) { ts.dsp_inhibit--; }
                ads.alc_val  = alc_holder;
            }
            else
            {
                AudioDriver_TxProcessor( (AudioSample_t*) src, (AudioSample_t*) dst );
            }
        } // END of TX Mode Thread


		// Perform LCD backlight PWM brightness function
		UiDriver_BacklightDimHandler();
		// tcount_n++;  if (tcount_n >= 9) { ts.sysclock_n++; tcount_n = 0; }  // 6 ms delay
         tcount   +=   CLOCKS_PER_DMA_CYCLE;   // add the number of clock cycles that would have passed between DMA cycles

    if ( tcount > CLOCKS_PER_CENTISECOND )	  // has enough clock cycles for 0.01 second passed?
    {
        tcount -= CLOCKS_PER_CENTISECOND;	  // yes - subtract that many clock cycles
        ts.sysclock++;	                      // 10ms pour les tempos des taches arri�re plan // this clock updates at PRECISELY 100 Hz over the long term
        //
        // Has the timing for the keyboard beep expired?

        if ( ts.sysclock > ts.beep_timing )
        {
            ts.beep_active = 0;				    // yes, turn the tone off
            ts.beep_timing = 0;
        }
    }


    if ( ts.scope_scheduler                   ) {  ts.scope_scheduler--;               }  // timer  pour affichage spectrum sur ecran
    if ( ts.waterfall.scheduler               ) {  ts.waterfall.scheduler--;           }  // timer  pour affichage waterfall sur ecran
    if ( ts.audio_spkr_unmute_delay_count     ) {  ts.audio_spkr_unmute_delay_count--; }  // this updates at 1.5 kHz - used to time TX->RX delay
    if ( ks.debounce_time < DEBOUNCE_TIME_MAX ) {  ks.debounce_time++;                 }  // keyboard debounce timer pour le clavier et les touches tactiles


#ifdef USE_PENDSV_FOR_HIGHPRIO_TASKS

    // let us trigger a pendsv irq here in order to trigger execution of UiDriver_HighPrioHandler()

    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
#endif
}


