/************************************************************************************
 **                                                                                 **
 **                               UHSDR Firmware                                    **
 **                                                                                 **
 **---------------------------------------------------------------------------------**
 **                                                                                 **
 **  Description:                                                                   **
 **  Licence:       GNU GPLv3                                                       **
 ************************************************************************************/

#include "uhsdr_board.h"
#include "audio_nr.h"
#include "arm_const_structs.h"
#include "profiling.h"

#include <string.h>             // N96K A2: memset() in AudioNr_Prepare()
#include "radio_management.h"   // N96K A2: DigitalMode_FreeDV for the NR/FreeDV buffer-sharing guard

//#define debug_alternate_NR

#ifdef USE_ALTERNATE_NR

NoiseReduction  __MCHF_SPECIALMEM 	NR;   // definition
NoiseReduction2 __MCHF_SPECIALMEM	NR2;  // definition

__IO int32_t  NR_in_head  = 0;
__IO int32_t  NR_in_tail  = 0;
__IO int32_t  NR_out_head = 0;
__IO int32_t  NR_out_tail = 0;

NR_Buffer*   NR_in_buffers [ NR_BUFFER_FIFO_SIZE ];
NR_Buffer*   NR_out_buffers[ NR_BUFFER_FIFO_SIZE ];

// N96K A2 (upstream df9ef3f9): these used to be function-local statics inside alternateNR_handle().
// They are at file scope now so that AudioNr_Prepare() can reset them when FreeDV is left.
static uint16_t  NR_current_buffer_idx = 0;
static bool      NR_was_here           = false;


 const   float32_t   SQRT_von_Hann_256[256]
= { 0          ,0.01231966 ,0.024637449,0.036951499,0.049259941,0.061560906,0.073852527,0.086132939,
	0.098400278,0.110652682,0.122888291,0.135105247,0.147301698,0.159475791,0.171625679,0.183749518,
	0.195845467,0.207911691,0.219946358,0.231947641,0.24391372 ,0.255842778,0.267733003,0.279582593,
	0.291389747,0.303152674,0.314869589,0.326538713,0.338158275,0.349726511,0.361241666,0.372701992,
	0.384105749,0.395451207,0.406736643,0.417960345,0.429120609,0.440215741,0.451244057,0.462203884,
	0.473093557,0.483911424,0.494655843,0.505325184,0.515917826,0.526432163,0.536866598,0.547219547,
	0.557489439,0.567674716,0.577773831,0.587785252,0.597707459,0.607538946,0.617278221,0.626923806,
	0.636474236,0.645928062,0.65528385 ,0.664540179,0.673695644,0.682748855,0.691698439,0.700543038,
	0.709281308,0.717911923,0.726433574,0.734844967,0.743144825,0.75133189 ,0.759404917,0.767362681,
	0.775203976,0.78292761 ,0.790532412,0.798017227,0.805380919,0.812622371,0.819740483,0.826734175,
	0.833602385,0.840344072,0.846958211,0.853443799,0.859799851,0.866025404,0.872119511,0.878081248,
	0.883909710,0.889604013,0.895163291,0.900586702,0.905873422,0.911022649,0.916033601,0.920905518,
	0.925637660,0.930229309,0.934679767,0.938988361,0.943154434,0.947177357,0.951056516,0.954791325,
	0.958381215,0.961825643,0.965124085,0.968276041,0.971281032,0.974138602,0.976848318,0.979409768,
	0.981822563,0.984086337,0.986200747,0.988165472,0.989980213,0.991644696,0.993158666,0.994521895,
	0.995734176,0.996795325,0.99770518 ,0.998463604,0.999070481,0.99952572 ,0.99982925 ,0.999981027,
	0.999981027,0.99982925 ,0.99952572 ,0.999070481,0.998463604,0.99770518 ,0.996795325,0.995734176,
	0.994521895,0.993158666,0.991644696,0.989980213,0.988165472,0.986200747,0.984086337,0.981822563,
	0.979409768,0.976848318,0.974138602,0.971281032,0.968276041,0.965124085,0.961825643,0.958381215,
	0.954791325,0.951056516,0.947177357,0.943154434,0.938988361,0.934679767,0.930229309,0.925637660,
	0.920905518,0.916033601,0.911022649,0.905873422,0.900586702,0.895163291,0.889604013,0.883909710,
	0.878081248,0.872119511,0.866025404,0.859799851,0.853443799,0.846958211,0.840344072,0.833602385,
	0.826734175,0.819740483,0.812622371,0.805380919,0.798017227,0.790532412,0.78292761 ,0.775203976,
	0.767362681,0.759404917,0.75133189 ,0.743144825,0.734844967,0.726433574,0.717911923,0.709281308,
	0.700543038,0.691698439,0.682748855,0.673695644,0.664540179,0.65528385 ,0.645928062,0.636474236,
	0.626923806,0.617278221,0.607538946,0.597707459,0.587785252,0.577773831,0.567674716,0.557489439,
	0.547219547,0.536866598,0.526432163,0.515917826,0.505325184,0.494655843,0.483911424,0.473093557,
	0.462203884,0.451244057,0.440215741,0.429120609,0.417960345,0.406736643,0.395451207,0.384105749,
	0.372701992,0.361241666,0.349726511,0.338158275,0.326538713,0.314869589,0.303152674,0.291389747,
	0.279582593,0.267733003,0.255842778,0.24391372 ,0.231947641,0.219946358,0.207911691,0.195845467,
	0.183749518,0.171625679,0.159475791,0.147301698,0.135105247,0.122888291,0.110652682,0.098400278,
	0.086132939,0.073852527,0.061560906,0.049259941,0.036951499,0.024637449,0.01231966 ,          0};

 /*
 const float32_t INV_SQRT_von_Hann_256[128]
 = {  1.409877843, 1.39292853 , 1.376588357, 1.360831257, 1.34563273 , 1.330969727, 1.31682055 , 1.303164754,
	  1.289983064, 1.277257292, 1.264970271, 1.25310578 , 1.241648487, 1.230583892, 1.219898274, 1.209578641,
	  1.19961269 , 1.18998876 , 1.180695797, 1.171723318, 1.163061378, 1.154700538, 1.14663184 , 1.138846778,
	  1.131337272, 1.124095649, 1.11711462 , 1.110387259, 1.103906987, 1.097667551, 1.091663012, 1.085887727,
	  1.080336338, 1.075003755, 1.069885147, 1.064975927, 1.060271747, 1.055768482, 1.051462224, 1.047349273,
	  1.043426127, 1.039689477, 1.036136198, 1.032763342, 1.029568134, 1.026547965, 1.023700386, 1.021023103,
	  1.018513974, 1.016171003, 1.013992336, 1.011976261, 1.010121199, 1.008425704, 1.00688846 , 1.00550828 ,
	  1.004284099, 1.003214978, 1.002300098, 1.00153876 , 1.000930384, 1.000474505, 1.000170779, 1.000018973,
	  1.000018973, 1.000170779, 1.000474505, 1.000930384, 1.00153876 , 1.002300098, 1.003214978, 1.004284099,
	  1.00550828 , 1.00688846 , 1.008425704, 1.010121199, 1.011976261, 1.013992336, 1.016171003, 1.018513974,
	  1.021023103, 1.023700386, 1.026547965, 1.029568134, 1.032763342, 1.036136198, 1.039689477, 1.043426127,
	  1.047349273, 1.051462224, 1.055768482, 1.060271747, 1.064975927, 1.069885147, 1.075003755, 1.080336338,
	  1.085887727, 1.091663012, 1.097667551, 1.103906987, 1.110387259, 1.11711462 , 1.124095649, 1.131337272,
	  1.138846778, 1.14663184 , 1.154700538, 1.163061378, 1.171723318, 1.180695797, 1.18998876 , 1.19961269,
	  1.209578641, 1.219898274, 1.230583892, 1.241648487, 1.25310578 , 1.264970271, 1.277257292, 1.289983064,
	  1.303164754, 1.31682055 , 1.330969727, 1.34563273 , 1.360831257, 1.376588357, 1.39292853 , 1.409877843 };
*/

/*
 const float32_t INV_von_Hann_256[128]
= {
	 1.987755532, 1.940249891, 1.894995504, 1.851861709, 1.810727443, 1.771480414, 1.734016361, 1.698238376,
	 1.664056304, 1.631386191, 1.600149787, 1.570274096, 1.541690965, 1.514336715, 1.488151798, 1.46308049 ,
	 1.439070607, 1.41607325 , 1.394042566, 1.372935535, 1.352711769, 1.333333333, 1.314764578, 1.296971983,
	 1.279924022, 1.263591028, 1.247945074, 1.232959865, 1.218610635, 1.204874052, 1.191728131, 1.179152156,
	 1.167126604, 1.155633074, 1.144654227, 1.134173726, 1.124176178, 1.114647088, 1.105572809, 1.0969405  ,
	 1.088738083, 1.080954209, 1.07357822 , 1.066600121, 1.060010543, 1.053800725, 1.04796248 , 1.042488177,
	 1.037370715, 1.032603506, 1.028180458, 1.024095953, 1.020344837, 1.0169224  , 1.013824371, 1.0110469  ,
	 1.008586551, 1.006440292, 1.004605487, 1.003079888, 1.001861633, 1.000949236, 1.000341587, 1.000037946,
	 1.000037946, 1.000341587, 1.000949236, 1.001861633, 1.003079888, 1.004605487, 1.006440292, 1.008586551,
	 1.0110469  , 1.013824371, 1.0169224  , 1.020344837, 1.024095953, 1.028180458, 1.032603506, 1.037370715,
	 1.042488177, 1.04796248 , 1.053800725, 1.060010543, 1.066600121, 1.07357822 , 1.080954209, 1.088738083,
	 1.0969405  , 1.105572809, 1.114647088, 1.124176178, 1.134173726, 1.144654227, 1.155633074, 1.167126604,
	 1.179152156, 1.191728131, 1.204874052, 1.218610635, 1.232959865, 1.247945074, 1.263591028, 1.279924022,
	 1.296971983, 1.314764578, 1.333333333, 1.352711769, 1.372935535, 1.394042566, 1.41607325 , 1.439070607,
	 1.46308049 , 1.488151798, 1.514336715, 1.541690965, 1.570274096, 1.600149787, 1.631386191, 1.664056304,
	 1.698238376, 1.734016361, 1.771480414, 1.810727443, 1.851861709, 1.894995504, 1.940249891, 1,987755532 };
 */

//  NIZZ 06 Nov 2022 :
//  fenetre de Hanning (0.50 + 0.50*cos(phi)) originale a été remplacée par
//  fenetre de Hamming (0.54 + 0.46*cos(phi))

 /*
void  NR_RAZ_Buffers ()
{
	arm_fill_f32 ( 0, NR.FFT_buffer          , 512 );
	arm_fill_f32 ( 0, NR.last_iFFT_result    , 128 );
	arm_fill_f32 ( 0, NR.last_sample_buffer_L, 128 );

 // arm_fill_f32 ( 0, NR2.X                  , 128 );

}
*/




int NR_in_buffer_remove(NR_Buffer** c_ptr)
{
    int ret = 0;

    if (NR_in_head  != NR_in_tail)
    {
        NR_Buffer* c = NR_in_buffers[NR_in_tail];
        NR_in_tail   = (NR_in_tail + 1) % NR_BUFFER_FIFO_SIZE;
        *c_ptr       = c;
        ret++;
    }
    return ret;
}

/* no room left in the buffer returns 0 */
int NR_in_buffer_add(NR_Buffer* c)
{
    int ret = 0;
    int32_t next_head = (NR_in_head + 1) % NR_BUFFER_FIFO_SIZE;

    if (next_head != NR_in_tail)
    {
        /* there is room */
        NR_in_buffers[NR_in_head] = c;
        NR_in_head = next_head;
        ret ++;
    }
    return ret;
}

void NR_in_buffer_reset()
{
    NR_in_tail = NR_in_head;
}

int8_t NR_in_has_data()
{
    int32_t len = NR_in_head - NR_in_tail;
    return  len < 0?len+NR_BUFFER_FIFO_SIZE:len;
}




//*********Out Buffer handling

int NR_out_buffer_peek(NR_Buffer** c_ptr)
{
    int ret = 0;

    if (NR_out_head != NR_out_tail)
    {
        NR_Buffer* c = NR_out_buffers[NR_out_tail];
        *c_ptr = c;
        ret++;
    }
    return ret;
}


int NR_out_buffer_remove ( NR_Buffer**  c_ptr )
{
    int ret = 0;

    if ( NR_out_head != NR_out_tail )
    {
        NR_Buffer* c =  NR_out_buffers [ NR_out_tail ];
        NR_out_tail  = (NR_out_tail + 1) % NR_BUFFER_FIFO_SIZE;
        *c_ptr = c;
        ret++;
    }
    return ret;
}

/* no room left in the buffer returns 0 */
int NR_out_buffer_add(NR_Buffer* c)
{
    int ret = 0;
    int32_t next_head = (NR_out_head + 1) % NR_BUFFER_FIFO_SIZE;

    if (next_head != NR_out_tail)
    {
        /* there is room */
        NR_out_buffers [NR_out_head ] = c;
        NR_out_head                   = next_head;
        ret ++;
    }
    return ret;
}

void NR_out_buffer_reset()
{
    NR_out_tail = NR_out_head;
}

int8_t NR_out_has_data()
{
    int32_t len = NR_out_head - NR_out_tail;
    return  len < 0? len + NR_BUFFER_FIFO_SIZE : len;
}

int32_t NR_out_has_room()
{
    // FIXME: Since we cannot completely fill the buffer
    // we need to say full 1 element earlier
    return NR_BUFFER_FIFO_SIZE - 1 - NR_out_has_data();
}

//**************************************************************************************************************

// N96K A2: backport of upstream df9ef3f9 (#1749) "AudioNr fix for silence after using FreeDV and NR".
// NR and FreeDV use the same MultiModeBuffer union (mmb). After using FreeDV the NR buffers may still
// contain FreeDV residue (values which are not valid audio and can even be NaN for the following IIR
// filters). Reset the NR processing state and clear the shared buffers when leaving FreeDV, so the
// first NR blocks after the switch are processed from silence instead of FreeDV data.
void AudioNr_Prepare(void)
{
    NR_current_buffer_idx = 0;
    NR_was_here           = false;
    memset(&mmb.nr_audio_buff[0].samples[0], 0, NR_BUFFER_NUM * sizeof(mmb.nr_audio_buff[0]));
}

//**************************************************************************************************************

void alternateNR_handle()
{
#ifdef USE_FREEDV
    // N96K A2 (upstream 27945d8b): NR and FreeDV use the same MultiModeBuffer union (mmb); while
    // FreeDV is active the NR algorithm must not run, otherwise it overwrites the FreeDV IQ buffer
    // and processes FreeDV residue as audio.
    if ( ts.dmod_mode == DEMOD_DIGI && ts.digital_mode == DigitalMode_FreeDV )
    {
        return;
    }
#endif

    if (NR_was_here == false)
    {
        NR_was_here           = true;
        NR_current_buffer_idx = 0;
        NR_in_buffer_reset();
        NR_out_buffer_reset();
    }

    if ( NR_in_has_data() && NR_out_has_room())
    {   // audio data is ready to be processed

        NR_current_buffer_idx %= NR_BUFFER_NUM;

        NR_Buffer* input_buf = NULL;
        NR_in_buffer_remove( &input_buf ); //  &input_buffer points to the current valid audio data

        // inside here do all the necessary noise reduction stuff!!!!!
        // here are the current input samples:  input_buf->samples
        // NR_output samples have to be placed here: fdv_iq_buff[NR_current_buffer_idx].samples
        // but starting at an offset of NR_FFT_SIZE_128 as we are using the same buffer for in and out
        // here is the only place where we are referring to fdv_iq... as this is the name of the used freedv buffer

        //profileTimedEventStart(ProfileTP8);

        do_alternate_NR( &input_buf->samples[0].real, &mmb.nr_audio_buff[NR_current_buffer_idx].samples[NR_FFT_SIZE_128].real);

        //profileTimedEventStop(ProfileTP8);

        NR_out_buffer_add( &mmb.nr_audio_buff[ NR_current_buffer_idx ] );
        NR_current_buffer_idx++;

    }

}
//********************************************************************************************************


void  do_alternate_NR ( float32_t*  inputsamples, float32_t*  outputsamples )
{

   // float32_t* Energy=0;

    /*
    if(ts.nb_setting > 0)      ///   NIZ32  masque pour retourner au Noise Blanker ancien
    {
        alt_noise_blanking ( inputsamples, NR_FFT_SIZE_128, Energy );  // la nouvelle fonction Noise-Blanker
    }
     */

    //    if(( ts.dsp_active & DSP_NR_ENABLE ) || ( ts.dsp_active & DSP_NOTCH_ENABLE ))
    if ( ts.dsp_active  &  DSP_NR_ENABLE )
    {
		// profileTimedEventStart ( ProfileTP8 );

		/*	// spectral_noise_reduction_2(inputsamples);
		if (ts.nr_mode == 0)
		  spectral_noise_reduction(inputsamples);
		else
		  if (ts.nr_mode == 1)
			spectral_noise_reduction_2(inputsamples);
		  else
			  */

		spectral_noise_reduction_3 ( inputsamples );

		// profileTimedEventStop ( ProfileTP8 );
    }

    for (int k=0; k < NR_FFT_SIZE_128;  k++)
    {
        outputsamples[k] = inputsamples[k];
    }

}
//***********************************************************************************************************


void spectral_noise_reduction_3 ( float* in_buffer )
{



				////////////////////////////////////////////////////////////////////////////////////////
				// Frank DD4WH & Michael DL2FW, November 2017
				// NOISE REDUCTION BASED ON SPECTRAL SUBTRACTION
				// following Romanin et al. 2009 on the basis of Ephraim & Malah 1984 and Hu et al. 2001
				// detailed technical description of the implemented algorithm
				// can be found in our WIKI
				// https://github.com/df8oe/UHSDR/wiki/Noise-reduction
				//
				// half-overlapping input buffers (= overlap 50%)
				// Hann window on 128 or 256 samples
				// FFT128 - inverse FFT128 or FFT256 / iFFT256
				// overlap-add


			//********************************************************************************************************
			//*************************   Initialisation des variables du Noise Reduction NR *************************
			//********************************************************************************************************


	int NR_FFT_LDD_128 = 128;   //  (ts.NR_FFT_L_256 >> 1u);

    if( ts.nr_first_time == 1 )      // TODO: properly initialize all the variables
    {
		for( int bindx = 0; bindx < NR_FFT_LDD_128 ; bindx++ )
		{
			  NR.last_sample_buffer_L[bindx]    = 0.0;
		}
        ts.nr_first_time = 2; // we need to do some more a bit later down
    }





    // NR_FFT_buffer is 256 floats big
    // interleaved r, i, r, i . . .
    // fill first half of FFT_buffer with last events audio samples

					//********************************************************************************************
					//*****************  Placement des nouveaux  128 points temporelles avec ceux décalés ********
					//********************************************************************************************
      float32_t  zero = 0.0 ;

	  for ( int i = 0; i < NR_FFT_LDD_128; i++ )
	  {
		int32_t    j = (i << 1u);
		NR.FFT_buffer [j     ] = NR.last_sample_buffer_L[i]; // real
		NR.FFT_buffer [j + 1u] = zero;                       // imaginary
	  }


	  for ( int i = 0; i < NR_FFT_LDD_128 ; i++ )  // copy recent samples to last_sample_buffer for next time!
	  {
		 NR.last_sample_buffer_L [i] = in_buffer[ i  ];
	  }


	  for (  int i = 0; i < NR_FFT_LDD_128; i++ )  // now fill recent audio samples into second half of FFT_buffer
	  {
		  NR.FFT_buffer[ ts.NR_FFT_L_256 + (i << 1u )     ] = in_buffer[i]; // real
		  NR.FFT_buffer[ ts.NR_FFT_L_256 + (i << 1u ) + 1u] = zero;
	  }


				   //***************************************************************************************
				   //*********  Fenetrage SQRT-VON-Hanning et FFT direct 256 points ************************
				   //***************************************************************************************
	  for ( int idx = 0; idx < ts.NR_FFT_L_256; idx++ )
	  {
		  NR.FFT_buffer[idx << 1u] *= SQRT_von_Hann_256[idx];         // FFT_buffer [re, im, re, im, re, im . . .]
	  }
	  arm_cfft_f32 ( &arm_cfft_sR_f32_len256, NR.FFT_buffer, 0, 1 );  // FFT_256   function


                     //**************************************************************************************
                     //*********** Calcul Amplitude au carré ************************************************
                     //**************************************************************************************
	 for ( int bindx = 0; bindx < NR_FFT_LDD_128; bindx++ )           //here we need squared magnitude
	 {
		int   bindxx = bindx << 1u;
		NR2.X[bindx] = (NR.FFT_buffer[bindxx] * NR.FFT_buffer[bindxx] + NR.FFT_buffer[bindxx + 1u] * NR.FFT_buffer[bindxx + 1u]);
	 }


		  // FINAL SPECTRAL WEIGHTING : Multiply current FFT results with NR_FFT_buffer for 64 bins
		  // with the 64 bin-specific gain factors
		  // only do this for the bins inside the filter passband
		  // if you do this for all the bins, you will get distorted audio: plopping !
		  //     for ( int bindx = 0; bindx < ts.NR_FFT_L_256 / 2; bindx++ ) // plopping !!!!
		  //*******************************************************************************************
		  //*******************  Application du Filtrage FFT par Pondération adaptative ***************
		  //*******************************************************************************************

    int32_t                 NR_FFT_LII_510 = ( ts.NR_FFT_L_256 << 1u ) - 2;

    // Ce buffer tampon FFT_buf[] servira de retarder la decision d'annulation de la raie spectrale
    // en vue de conserver la raie sans annulation si elle a été déja presente au précedent ou au prochain spectre
    // ceci servira à réduire les distorsions , à mieux conserver les transitoires de la parole
    // et surtout à beaucoup réduire le bruit de chute d'eau caractéridtique des Noise reductions.

 //   static    float32_t     FFT_last_buf[ 512 ];  // il n'y a plus despace dans la memoire speciale 64Ko

   // static    int8_t        track[128];

    // float32_t  coeff_red  = 0.5;

	if ( ts.doSecondDecimation )
	{
		 static  uint16_t  __MCHF_SPECIALMEM   last_zero[ 128 ];  // 1er bit = Last , 2em bit = Last_last , 2 booleans dans un seul octet

		// static  uint8_t     un    = 0x1;
		// static  uint8_t     deux  = 0x2;
		// static  uint8_t     trois = 0x3;


	 //  static  uint8_t     quatre= 0x4;
	 //  static  uint8_t     cinq  = 0x5;
	 //  static  uint8_t     six   = 0x6;
	//	 static   int8_t     Max_track = 70;
	//	 static   int8_t     Min_track =-70;

	//	 Max_track =  ts.debug_coeff_1;  // debugage
	//	 Max_track = -ts.debug_coeff_1;  // debugage


		         float32_t   zzero    = 0.0;
		     //   uint32_t   max_band = 118;

		 	 	 // Annulation de la composante continue et tres basse frequence
		         NR.FFT_buffer [   0] = 0.0;    // real part
		         NR.FFT_buffer [ 511] = zzero;    // imag part conjugate symmetric

		         NR.FFT_buffer [   1] = 0.0;    // imag part
		         NR.FFT_buffer [ 510] = zzero;    // real part conjugate symmetric



		// int16_t   raie_zzero = 0;
		float32_t  val_seuil   = ( (uint32_t) ts.dsp_nr_strength  + 25 ) * 1600000.0;

		uint16_t   Masque  = 0x0FFF;    //  ts.DNR_Masque;   12 x 21.33 ms = 256 ms

		for ( int bindx = 2;    bindx < NR_FFT_LDD_128;  bindx++ )   // 8 semble optimal // 110=2580 Hz
		{

			  //if (last_zero[bindx])

			if  ( NR2.X[bindx] < (val_seuil  - (bindx << 19u) )) //
			{
				int32_t        bindxx     =  (bindx   <<  1u );
				int32_t        NR_FFT_LL  =  NR_FFT_LII_510 - bindxx;

				if (bindx < 108)    //   freq < 2531 Hz
				{
					  NR.FFT_buffer [ bindxx        ] = zzero;    // real part
					  NR.FFT_buffer [ NR_FFT_LL     ] = 0;    // real part conjugate symmetric
					  NR.FFT_buffer [ bindxx    + 1u] = zzero;    // imag part
					  NR.FFT_buffer [ NR_FFT_LL + 1u] = 0;    // imag part conjugate symmetric
				}
				else               //    freq >= 2531 Hz  Reduction -7dB seulement
				{
					  NR.FFT_buffer [ bindxx        ] *= 0.45;    // real part
					  NR.FFT_buffer [ NR_FFT_LL     ] *= 0.45;    // real part conjugate symmetric
					  NR.FFT_buffer [ bindxx    + 1u] *= 0.45;    // imag part
					  NR.FFT_buffer [ NR_FFT_LL + 1u] *= 0.45;    // imag part conjugate symmetric
				}
			    last_zero [ bindx ]  <<=   1u;
			}
			else
			{
				last_zero [ bindx ]  <<=   1u;
				last_zero [ bindx ]   |=   1u;

				if (bindx > 7)  // freq > 164 Hz  // Masque Reduction
				{
					if ((last_zero[ bindx] & Masque) == Masque)     // 0x0FFF
					{
						  int32_t        bindxx     =  (bindx   <<  1u );
						  int32_t        NR_FFT_LL  =  NR_FFT_LII_510 - bindxx;

						  NR.FFT_buffer [ bindxx        ] *= 0.3;    // real part    (-10.5 dB)
						  NR.FFT_buffer [ NR_FFT_LL     ] *= 0.3;    // real part conjugate symmetric
						  NR.FFT_buffer [ bindxx    + 1u] *= 0.3;    // imag part
						  NR.FFT_buffer [ NR_FFT_LL + 1u] *= 0.3;    // imag part conjugate symmetric
					}
				}
			}

			//if  ( NR2.X[bindx] < (val_seuil  - ((bindx + track[bindx]) << 19u) )) // || bindx > max_band )    // Eliminer la composante continue et basse du spectre
			//  if  ( NR2.X[bindx] < (val_seuil  - (bindx << 19u) )) // || bindx > max_band )    // Eliminer la composante continue et basse du spectre
			//  {
			      // if ( last_zero[bindx] == trois )   // ( Last_zero =1 et Last_Last_Zero = 1) en meme temps


			//	  last_zero[bindx] = true ;
			      // raie_zzero++;

			     // if ( track[bindx] < Max_track ) track[bindx] += cinq;  // ts.debug_coeff_2; // on baisse le seuil d'avantage


				 // last_zero [ bindx ]  <<=   1u;          // decalage du last vers lastlast
				//  last_zero [ bindx ]   |=   un;          // forcer true au last
			     // last_zero [ bindx ]   &=  trois;        // masquer les 6 bits du surplus/octer
			//  }
			//  else last_zero[bindx] = false ;


			/*  {

				 if ( ( ts.dsp_active & DSP_NOTCH_ENABLE ) )
				 {
					  if ( ! last_zero[bindx] )
					  {
						  val_seuil = 700.0E6 - (bindx << 20u);

						  if  ( NR2.X[bindx] >  val_seuil)    // Eliminer la composante continue et basse du spectre
						  {
							  float32_t  coeff_red = sqrtf (val_seuil/NR2.X[bindx]);

							  int32_t        bindxx      =  (bindx   <<  1u);
							  int32_t        NR_FFT_LL   =  NR_FFT_LII_510 - bindxx;

							  FFT_last_buf [ bindxx        ] *= coeff_red;    // real part
							  FFT_last_buf [ bindxx    + 1u] *= coeff_red;    // imag part
							  FFT_last_buf [ NR_FFT_LL     ] *= coeff_red;    // real part conjugate symmetric
							  FFT_last_buf [ NR_FFT_LL + 1u] *= coeff_red;    // imag part conjugate symmetric
						  }
					  }

				  }

				 // if ( track[bindx] > Min_track ) track[bindx] -= cinq; //  ts.debug_coeff_2;  // on hausse le seuil


				 // last_zero [ bindx ] <<=  1u;           // decalage du last vers lastlast
				 // last_zero [ bindx ]  &=  deux;         // mettre false au Last et masquer les 6bits du surplus/octer
			  }*/
		}

	/*	ts.raie_zero += ((float32_t)raie_zzero - ts.raie_zero) /6.0;

		  if ( raie_zzero <  (ts.pdp_alpha_int -500)  )
		  {
			  for ( int bindx = 1;    bindx < NR_FFT_LDD_128;  bindx++ )
			  {
				  int32_t        bindxx     =  (bindx   <<  1u );
				  int32_t        NR_FFT_LL  =  NR_FFT_LII_510 - bindxx;

				  FFT_last_buf [ bindxx        ] = zzero;    // real part
				  FFT_last_buf [ NR_FFT_LL     ] = zzero;    // real part conjugate symmetric
				  FFT_last_buf [ bindxx    + 1u] = zzero;    // imag part
				  FFT_last_buf [ NR_FFT_LL + 1u] = zzero;    // imag part conjugate symmetric
			  }
		  }*/
	}
	else  // pas de seconde décimation vers 6Khz , on reste a 12Khz
	{

		   float32_t  zzero      = 0.0 ;
		   float32_t  val_seuil  = ( (uint32_t) ts.dsp_nr_strength  + 23 ) * 1500000.0;

		   	   	   // Annulation de la composante continue et tres basse frequence
		 NR.FFT_buffer [   0 ] = zzero; // real part
		 NR.FFT_buffer [   1 ] = zzero; // imag part
		 NR.FFT_buffer [ 510 ] = zzero; // real part conjugate symmetric
		 NR.FFT_buffer [ 511 ] = zzero; // imag part conjugate symmetric

		for ( uint  bindx = 2;    bindx < NR_FFT_LDD_128;  bindx++ )
		{
			  if (( NR2.X[bindx] < (val_seuil  - (bindx << 19u) ) )   || (bindx > 70) ) // Eliminer la composante continue du spectre et audela de 3300 Hz
			  {
				  uint32_t                 bindxx     =  (bindx   <<  1u) ;
				  uint32_t                 NR_FFT_LL  =  NR_FFT_LII_510 - bindxx;

				  NR.FFT_buffer [ bindxx        ] = zzero; // real part
				  NR.FFT_buffer [ bindxx    + 1u] = zzero; // imag part
				  NR.FFT_buffer [ NR_FFT_LL     ] = zzero; // real part conjugate symmetric
				  NR.FFT_buffer [ NR_FFT_LL + 1u] = zzero; // imag part conjugate symmetric
			  }
		}
	}

	    					 //*****************************************************************
							 //*** NOISE REDUCTION ESTIMATION ENDS HERE
							 //*****************************************************************


               //****************************************************************************************
               //*************   FFT-inverse    256 points **********************************************
               //****************************************************************************************

    arm_cfft_f32 ( &arm_cfft_sR_f32_len256,  NR.FFT_buffer, 1, 1 );  /// FFT-INVERSE

			   //****************************************************************************************
			   //*************   Fenetrage SQRT-Von-Hanning   256 points ********************************
			   //****************************************************************************************

    for ( int idx = 0; idx <  ts.NR_FFT_L_256 ; idx++ )
	{
    	NR.FFT_buffer[ idx  << 1u ] *= SQRT_von_Hann_256[idx];
	}

				//*********************************************************************************************
				//*****************************  Reassemblage des 128 points temporelles **********************
				//*********************************************************************************************
				// do the overlap & add

	for ( int i = 0;  i < NR_FFT_LDD_128;  i++ )
	{ 				// take real part of first half of current iFFT result and add to 2nd half of last iFFT_result
	   in_buffer[ i  ] = NR.FFT_buffer[ i << 1u ] + NR.last_iFFT_result[i];
	}


	for ( int i = 0; i < NR_FFT_LDD_128; i++ )
	{
		NR.last_iFFT_result[i] = NR.FFT_buffer[ ts.NR_FFT_L_256 + (i << 1u) ];
	}

//	arm_copy_f32 ( &NR.FFT_buffer[0], FFT_last_buf, 512 ); // sauvegarde pour differer le traitement du spectre d'une sequence

					//*********************************************************************************************
					//*****************************  Selection des 128 points du milieu  **********************
					//*********************************************************************************************
 /*
	  static  bool  signe_inverse = false;

	  signe_inverse = ! signe_inverse ;

	  if (signe_inverse)
	  {

		  for ( int idx = 0; idx < NR_FFT_LDD_128; idx++ )
		  {
			  in_buffer[ idx  ] = -NR.FFT_buffer[ (idx + 64 ) << 1 ] * INV_SQRT_von_Hann_256[idx];
		  }
	  }
	  else
	  {
		  for ( int idx = 0; idx < NR_FFT_LDD_128; idx++ )
		  {
			  in_buffer[ idx  ] =  NR.FFT_buffer[ (idx + 64 ) << 1 ] * INV_SQRT_von_Hann_256[idx];
		  }
	  }
*/

}

//*****************************************************************************************************
//*****************************************************************************************************



#endif
