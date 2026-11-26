/*  -*-  mode: c; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4; coding: utf-8  -*-  */
/************************************************************************************
 **                                                                                 **
 **                                        UHSDR                                    **
 **               a powerful firmware for STM32 based SDR transceivers              **
 **                                                                                 **
 **---------------------------------------------------------------------------------**
 **                                                                                 **
 **  File name:                                                                     **
 **  Description:                                                                   **
 **  Last Modified:                                                                 **
 **  Licence:		GNU GPLv3                                                      **
 ************************************************************************************/

// Common
#include "radio_management.h"
#include "uhsdr_board.h"
#include "profiling.h"
#include "adc.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <soft_tcxo.h>

#include "uhsdr_hw_i2c.h"

#include "freedv_uhsdr.h"
// SI570 control
#include "osc_interface.h"
#include "codec.h"
#include "audio_driver.h"
#include "audio_management.h"

#include "cat_driver.h"
#include "ui_spectrum.h"


#include "ui_configuration.h"
#include "ui_menu.h"  // for CONFIG_160M_FULL_POWER_ADJUST and Co.

#include "config_storage.h"

#include "cw_gen.h"
#include "freedv_api.h"
#include "codec2_fdmdv.h"

#include "cw_decoder.h"

#include "psk.h"
#include "rtty.h"
#include "ui_driver.h"

#define SWR_SAMPLES_SKP             1   //5000
#define SWR_SAMPLES_CNT             5//10
#define SWR_ADC_FULL_SCALE          4095    // full scale of A/D converter (4095 = 10 bits)
#define SWR_ADC_VOLT_REFERENCE      3.3     // NOMINAL A/D reference voltage.  The PRECISE value is calibrated by a menu item!  (Probably "FWD/REV ADC Cal.")
//
// coefficients for very low power (<75 milliwatt) power levels.  Do NOT use this above approx. 0.07 volts input!
//
#define LOW_RF_PWR_COEFF_A          -0.0338205168744131     // constant (offset)
#define LOW_RF_PWR_COEFF_B          5.02584652062682        // "b" coefficient (for x)
#define LOW_RF_PWR_COEFF_C          -106.610490958242       // "c" coefficient (for x^2)
#define LOW_RF_PWR_COEFF_D          853.156505329744        // "d" coefficient (for x^3)
//
// coefficients for higher power levels (>50 milliwatts).  This is actually good down to 25 milliwatts or so.
//
#define HIGH_RF_PWR_COEFF_A         0.01209 //0.0120972709513557        // constant (offset)
#define HIGH_RF_PWR_COEFF_B         0.8334  //0.833438917330908     // "b" coefficient (for x)
#define HIGH_RF_PWR_COEFF_C             1.569   //1.56930042559198      // "c" coefficient (for x^2)

#define LOW_POWER_CALC_THRESHOLD        0.05    // voltage from sensor below which we use the "low power" calculations, above

// N96K P0-4: VSWR foldback protection (audit U3, adapted from upstream 2.12.2 dcbebe5f)
// N96K P1-3: own measurement counters, no dependency on the TX display settle counter (audit R1/R5)
#define VSWR_MAX_CALC_VALUE             49.0f   // cap for (nearly) infinite mismatch: avoids inf/NaN readings
#define VSWR_PROTECTION_HYSTERESIS      0.5f    // foldback is released only below (threshold - hysteresis)
// N96K P1-3: number of 10 ms measurement ticks (ADC sample pairs) after the start of a transmission
// before the protection may act. RadioManagement_UpdatePowerAndVSWR() is called every 10 ms, so this
// is ~30 ms - the old code waited ~190 ms via ts.dynamic_tune_activ_counter and any tuning action
// could suppress the protection completely (audit R1/R5). A complete VSWR measurement is averaged over
// SWR_SAMPLES_CNT (5) samples, therefore the first possible foldback decision is taken after ~60 ms.
#define VSWR_PROTECTION_ARM_CNT         3
#define VSWR_PROTECTION_RECOVERY_CNT    10      // consecutive good measurements (~0.6 s) before the PA output is restored
#define VSWR_PROTECTION_RETRY_CNT       8       // measurements between two recovery trials (~0.5 s)

// N96K P1-1: the VSWR protection threshold is a run-time configuration value, see ts.vswr_protection_threshold

// SWR/Power meter
SWRMeter                    swrm;

// N96K P0-4: VSWR foldback state is reset when returning to RX (see RadioManagement_SwitchTxRx)
static void                 RadioManagement_VswrFoldbackReset(void);

// ------------------------------------------------
// Frequency public
DialFrequency               df;


#define BandInfoGenerate(BAND,SUFFIX,NAME) { BAND_FREQ_##BAND , BAND_SIZE_##BAND , NAME }

const BandInfo bandInfo[] =
{
    BandInfoGenerate(  80, M,   "80m"),
    BandInfoGenerate(  60, M,   "60m"),
    BandInfoGenerate(  40, M,   "40m"),
    BandInfoGenerate(  30, M,   "30m"),
    BandInfoGenerate(  20, M,   "20m"),
    BandInfoGenerate(  17, M,   "17m"),
    BandInfoGenerate(  15, M,   "15m"),
    BandInfoGenerate(  12, M,   "12m"),
    BandInfoGenerate(  10, M,   "10m"),
    BandInfoGenerate(   6, M,    "6m"),
    BandInfoGenerate(   4, M,    "4m"),
    BandInfoGenerate(   2, M,    "2m"),
    BandInfoGenerate(  70, CM, "70cm"),
    BandInfoGenerate(  23, CM, "23cm"),
    BandInfoGenerate(2200, M, "2200m"),
    BandInfoGenerate( 630, M,  "630m"),
    BandInfoGenerate( 160, M,  "160m"),
    { 0, 0, "Gen" } // Generic Band
};


// The following descriptor table has to be in the order of the enum digital_modes_t in  radio_management.h
// This table is stored in flash (due to const) and cannot be written to
// for operational data per mode [r/w], use a different table with order of modes
const digital_mode_desc_t digimodes[DigitalMode_Num_Modes] =
{
    { "DIGITAL" , true },
#ifdef USE_FREEDV
    { "FreeDV"  , true },
#endif
    { "RTTY"    , true },
    { "BPSK"    , true },
};


/**
 * @brief returns the "real" frequency translation mode for a given transceiver state. This may differ from the configured one due to modulation demands
 *
 */
uint32_t RadioManagement_GetRealFreqTranslationMode(uint32_t txrx_mode, uint32_t dmod_mode, uint32_t iq_freq_mode)
{
    uint32_t retval = iq_freq_mode;
    if (dmod_mode == DEMOD_CW && txrx_mode == TRX_MODE_TX)
    {
        retval = FREQ_IQ_CONV_MODE_OFF;
    }
    return retval;
}


/**
 * @brief permits to dis/enable a digital codec (or get back to analog)
 */
void RadioManagement_ChangeCodec(uint32_t codec, bool enableCodec)
{
    // codec == 0 -> Analog Sound
    // all other codecs -> digital codec
    if (codec == 0)
    {
        ts.dvmode = false;
    }
    else
    {
        ts.dvmode = enableCodec;
    }
    ts.digital_mode = codec;
}


/**
 * @brief API Function, implements application logic for changing the power level including filter changes
 *
 *
 * @param power_level The requested power level (as PA_LEVEL constants)
 * @returns true if power level has been changed, false otherwise
 */
bool RadioManagement_PowerLevelChange(uint8_t band, uint8_t power_level)
{
    bool retval = false;

    if(ts.dmod_mode == DEMOD_AM)                // in AM mode?
    {
        if(power_level >= PA_LEVEL_TUNE_KEEP_CURRENT)     // yes, power over 2 watts?
        {
            power_level = PA_LEVEL_2W;  // force to 2 watt mode when we "roll over"
        }
    }
    else        // other modes, do not limit max power
    {
        if(power_level >= PA_LEVEL_TUNE_KEEP_CURRENT)
        {
            power_level = PA_LEVEL_FULL; //  0 == full power
        }
    }

    if (power_level   != ts.power_level)
    {
        retval         = true;
        ts.power_level = power_level;
        RadioManagement_SetBandPowerFactor(band); // Set TX power factor - to reflect changed power
        // on a laiss� cette fonction car  elle  fonctionne aussi en modifiant la puissance durant Txing
    }
    return retval;
}

bool RadioManagement_Tune(bool tune)
{
    bool retval = tune;
    if(RadioManagement_IsTxDisabled() == false &&  (ts.dmod_mode != DEMOD_SAM))
    {
        if(tune)   ///  Tx Tune ON
        {
            if(ts.tune_power_level != PA_LEVEL_TUNE_KEEP_CURRENT)
            {
                ts.power_temp  = ts.power_level;             //store tx level and set tune level
                ts.power_level = ts.tune_power_level;
            }
            // DDS on

            RadioManagement_SwitchTxRx(TRX_MODE_TX,true);                // tune ON
            retval = (ts.txrx_mode  == TRX_MODE_TX);
        }
        else
        {
        	RadioManagement_SwitchTxRx(TRX_MODE_RX,true);                // tune OFF
            if(ts.tune_power_level != PA_LEVEL_TUNE_KEEP_CURRENT)
            {
                ts.power_level = ts.power_temp; // restore tx level
            }

            retval = false; // no longer tuning
        }
    }
    else
    {
        retval = false;    // no TUNE mode in AM or FM or with disabled TX!
    }
    return retval;
}

/**
 * @returns offset of tuned frequency to dial frequency in CW mode, in Hz
 */
int32_t RadioManagement_GetCWDialOffset()
{

    int32_t retval = 0;

    switch(ts.cw_offset_mode)
    {
		case CW_OFFSET_USB_SHIFT:    // Yes - USB?
		case CW_OFFSET_LSB_SHIFT:   // LSB?
		case CW_OFFSET_AUTO_SHIFT:  // Auto mode?  Check flag
									if(ts.cw_lsb)
									{
										retval += ts.cw_sidetone_freq;          // it was LSB - raise by sidetone amount
									}
									else
									{
										retval -= ts.cw_sidetone_freq;          // it was USB - lower by sidetone amount
									}
    }

    return retval ;
}

/**
 * @brief returns true if the mode parameters tell us we will TX at zero if as opposed to offset frequency
 */
static bool RadioManagement_IsTxAtZeroIF(uint8_t dmod_mode, uint8_t digital_mode)
{
    return  (
                dmod_mode == DEMOD_CW ||
                (dmod_mode == DEMOD_DIGI &&
                    (
#ifdef USE_FREEDV
                            digital_mode == DigitalMode_FreeDV
#else
                            false
#endif
                            ||
                            digital_mode == DigitalMode_BPSK
                    )
                )
             );
}
uint32_t RadioManagement_Dial2TuneFrequency(const uint32_t dial_freq, uint8_t txrx_mode)
{
    uint32_t tune_freq = dial_freq;

    //
    // Do "Icom" style frequency offset of the LO if in "CW OFFSET" mode.  (Display freq. is also offset!)
    if(ts.dmod_mode == DEMOD_CW)            // In CW mode?
    {
        tune_freq += RadioManagement_GetCWDialOffset() ;
    }


    // Offset dial frequency if the RX/TX frequency translation is active and we are not transmitting in mode which
    // does not use frequency translation, this permits to use the generated I or Q channel as audio sidetone
    // that is right now RTTY, FreeDV, CW, BPSK
    if(txrx_mode != TRX_MODE_TX || RadioManagement_IsTxAtZeroIF(ts.dmod_mode, ts.digital_mode) == false)
    {
        tune_freq += AudioDriver_GetTranslateFreq();
    }

    // Extra tuning actions
    if(txrx_mode == TRX_MODE_RX)
    {
        tune_freq += (ts.rit_value*10)  ; // 20   Add RIT on receive   NIZZZ   -8 Hz pour un l�ger d�calage en Rx
    }

    return tune_freq;
}

/**
 * @brief switch off the PA Bias to mute HF output ( even if PTT is on )
 * Using this method the PA will be effectively muted no matter what setting
 * the main bias switch has (which directly connected to the PTT HW Signal)
 * Used to suppress signal path reconfiguration noise during rx/tx and tx/rx switching
 */
void RadioManagement_DisablePaBias()
{
    Board_SetPaBiasValue(0);
}

/**
 * @brief recalculate and set the PA Bias according to requested value
 *
 * Please note that at the mcHF BIAS is only applied if the PTT HW Signal
 * is active (which is controlled using MchfBoard_EnableTXSignalPath())
 */
void RadioManagement_SetPaBias()
{

    uint32_t   calc_var;

    if((ts.pa_cw_bias) && (ts.dmod_mode == DEMOD_CW))       // is CW PA bias non-zero AND are we in CW mode?
    {
        calc_var = ts.pa_cw_bias;       // use special CW-mode bias setting
    }
    else
    {
        calc_var = ts.pa_bias;      // use "default" bias setting
    }
    calc_var = calc_var * 2 + BIAS_OFFSET;

    if(calc_var > 255)
    {
        calc_var = 255;
    }
    Board_SetPaBiasValue(calc_var);
}


bool RadioManagement_ChangeFrequency(bool force_update, uint32_t dial_freq,uint8_t txrx_mode)
{
    // everything else uses main VFO frequency
    // uint32_t    tune_freq;

    bool   lo_change_pending  =  false;

    // Calculate actual tune frequency
    ts.tune_freq_req = RadioManagement_Dial2TuneFrequency ( dial_freq, txrx_mode );

    if((ts.tune_freq != ts.tune_freq_req) || (ts.refresh_freq_disp) || df.temp_factor_changed || force_update )  // did the frequency NOT change and display refresh NOT requested??

    {

        if(ts.sysclock-ts.last_tuning > 5 || ts.last_tuning == 0)     // prevention for SI570 crash due too fast frequency changes
        {
            Oscillator_ResultCodes_t lo_prep_result = osc->prepareNextFrequency(ts.tune_freq_req, df.temp_factor);

            								// first check and mute output if a large step is to be done
            if(osc->isNextStepLarge() == true)     // did the tuning require that a large tuning step occur?
            {
                // 18 is a little more than 10ms (15 ==10ms) which is max for the Si570 to change the frequency

                if ( ts.audio_dac_output_muting_buffer_count < 18)  { ts.audio_dac_output_muting_buffer_count = 18; }
                if ( ts.audio_processor_input_mute_counter   < 18)  { ts.audio_processor_input_mute_counter   = 18; }
            }

            ts.last_tuning     =   ts.sysclock;
            ts.last_lo_result  =   lo_prep_result;

            Oscillator_ResultCodes_t lo_exec_result = OSC_TUNE_IMPOSSIBLE;
            if (lo_prep_result != OSC_TUNE_IMPOSSIBLE)
            {
                // Set frequency
                lo_exec_result = osc->changeToNextFrequency();
            }

            // if i2c error or verify error, there is a chance that we can fix that, so we mark this
            // as NOT executed, in all other cases we assume the change has happened (but may prevent TX)
            if (lo_exec_result != OSC_COMM_ERROR && lo_exec_result != OSC_ERROR_VERIFY)
            {
                df.temp_factor_changed = false;
                ts.tune_freq           = ts.tune_freq_req;        // frequency change required - update change detector
                // Save current freq
                df.tune_old            = dial_freq;
            }
            else
            {
                ts.last_lo_result = lo_exec_result;
            }

            if (ts.last_lo_result == OSC_OK || ts.last_lo_result == OSC_TUNE_LIMITED)
            {

                ts.tx_disable &= ~TX_DISABLE_OUTOFRANGE;
            }
            else
            {
                ts.tx_disable |= TX_DISABLE_OUTOFRANGE;
            }

            uint32_t tune_freq_real = ts.tune_freq;

            RadioManagement_SetHWFiltersForFrequency ( tune_freq_real );  // check the filter status with the new frequency update
            AudioManagement_CalcIqPhaseGainAdjust    ( tune_freq_real );

            // Inform Spectrum Display code that a frequency change has happened
            ts.dial_moved = 1;
        }
        else
        {
            lo_change_pending = true;
        }

    }

    // successfully executed the change
    return lo_change_pending == false;
}

/**
 * @brief temporary muting of the receiver when making changes which may cause audible pops etc., unmuting happens some 10s of milliseconds automatically later
 *
 */
void RadioManagement_MuteTemporarilyRxAudio()
{
    // save us from the loud "POP" that will occur when we change bands
    ts.audio_processor_input_mute_counter    =  5 * 15;  // Mute entr�e Codec vers Rx_processing
    ts.audio_dac_output_muting_buffer_count  = 13 * 15;  // Mute sortie codec vers Speaker
}

Oscillator_ResultCodes_t RadioManagement_ValidateFrequencyForTX(uint32_t dial_freq)
{
    // N96K P0-5: out-of-band TX interlock (audit U4). TX outside of the amateur bands is only
    // allowed with CONFIG_TX_OUT_ENABLE / FLAGS1_TX_OUTSIDE_BANDS (default: RX only).
    // This keeps the radio in RX instead of keying the PA on a frequency with a wrong LPF.
    if ((RadioManagement_GetBand(dial_freq) >= MAX_BANDS) && ((ts.flags1 & FLAGS1_TX_OUTSIDE_BANDS) == 0))
    {
        return OSC_TUNE_IMPOSSIBLE;
    }

    // we check with the si570 code if the frequency is tunable, we do not tune to it.
    return osc->prepareNextFrequency(RadioManagement_Dial2TuneFrequency(dial_freq, TRX_MODE_TX), df.temp_factor);
}

/**
 * @brief returns the current LO Tune Frequency for TX
 * @returns LO Frequency in Hz
 */
uint32_t RadioManagement_GetTXDialFrequency()
{
    uint32_t retval;
    if (ts.txrx_mode != TRX_MODE_TX)
    {
        if(is_splitmode())                  // is SPLIT mode active and?
        {
            uint8_t              vfo_tx;
            if   (is_vfo_b())  { vfo_tx = VFO_A; }
            else               { vfo_tx = VFO_B; }
            retval = vfo[vfo_tx].band[ts.band].dial_value;    // load with VFO-A frequency
        }
        else
        {
            retval = df.tune_new;
        }
    }
    else
    {
        retval = df.tune_new;
    }
    return retval;
}
/**
 * @brief returns the current LO Dial Frequency for RX
 * @returns LO Frequency in Hz,
 */
uint32_t RadioManagement_GetRXDialFrequency()
{
    uint32_t baseval;
    if (ts.txrx_mode != TRX_MODE_RX)
    {
        if(is_splitmode())                  // is SPLIT mode active?
        {
            uint8_t vfo_rx;
            if (is_vfo_b())
            {
                vfo_rx = VFO_B;
            }
            else
            {
                vfo_rx = VFO_A;
            }
            baseval = vfo[vfo_rx].band[ts.band].dial_value;    // load with VFO-A frequency
        }
        else
        {
            baseval = df.tune_new;
        }
    }
    else
    {
        baseval = df.tune_new;
    }

    return baseval + (ts.rit_value*10  );  ///  NIZZZ
}

//*******************************************************************************************
//*********  Pour Initialiser les filtres Tx qui sont en decimation 1/4 si besoin  **********
//*******************************************************************************************

void   RadioManagement_Check_Initialise_Tx_decimated_filters (bool tune_now)     // Ajouté Par NIZZZ
{
	if    (( ts.tx_audio_source != TX_AUDIO_DIG_IQ)   && is_ssb(ts.dmod_mode) )  // && ! tune_now)
	{
		if ( ts.tx_decimatedIQ == false )
		{
			 ts.tx_decimatedIQ  = true;		// // ts.tx_decimatedIQ = ( (USB + LSB) & not_DIG_IQ )
			 AudioDriver_Set_Tx_Audio_Equalizer();               // equaliseur Basse-treeble du Tx (le Rx sera refait sans changement)
			 AudioDriver_Voice_Tx_Filter_Init ( ts.dmod_mode );  // Low pass Filter 2850 Hz
			 AudioFilter_Init_Tx_Hilbert_FIR();                  // Hilbert
		}
	}
	else
	{
		if ( ts.tx_decimatedIQ == true  )
		{
			 ts.tx_decimatedIQ  = false;
			 AudioDriver_Set_Tx_Audio_Equalizer();               // equaliseur Basse-treeble du Tx (le Rx sera refait sans changement)
			 AudioDriver_Voice_Tx_Filter_Init ( ts.dmod_mode);   // Low pass Filter 2850 Hz
			 AudioFilter_Init_Tx_Hilbert_FIR();					 // Hilbert
		}
	}
}
//***************************************************************************************************





// globals used:
// ts.trx_mode -> R / W
// ts.band -> R
// vfo[] -> r / w
// df.tune_new -> r
// ts.dmod_mode -> r
// ts.cw_text_entry -> r
// functions called:
// RadioManagement_ValidateFrequencyForTX
// RadioManagement_IsTxDisabled()
// ts.audio_dac_output_muting_buffer_count -> r/w
// ts.audio_dac_output_muting_flag = true; // let the audio being muted initially as long as we need it
// ads.agc_holder -> w
// ads.agc_val -> r
// RadioManagement_DisablePaBias(); // kill bias to mute the HF output quickly
// Board_RedLed(LED_STATE_ON); // TX
// Board_GreenLed(LED_STATE_OFF);
// Board_EnableTXSignalPath(true); // switch antenna to output and codec output to QSE mixer
// RadioManagement_ChangeFrequency(false,df.tune_new, txrx_mode_final);
// uint8_t tx_band = RadioManagement_GetBand(tune_new);
// RadioManagement_PowerLevelChange(tx_band,ts.power_level);
// RadioManagement_SetBandPowerFactor(tx_band);
// AudioManagement_SetSidetoneForDemodMode(ts.dmod_mode,txrx_mode_final == TRX_MODE_RX?false:tune_mode);
// Codec_SwitchTxRxMode(txrx_mode_final);
// RadioManagement_SetPaBias();
// ts.txrx_switch_audio_muting_timing -> r;
// ts.audio_processor_input_mute_counter -> w;
// ts.audio_dac_output_muting_buffer_count -> w
// ts.audio_dac_output_muting_flag -> w;
// ts.tx_audio_source -> r
// ts.power_level -> r

void   RadioManagement_SwitchTxRx ( uint8_t  txrx_requested_mode,  bool  tune_mode )
{

    uint32_t         tune_new;
    bool             tx_ok          = false;
    bool             tx_pa_disabled = false;

    // ts.last_tuning = 0;                  // prevents transmitting on wrong frequency during "RX bk phases"
    ts.switch_pause   = 1000; // attendre 2000 trames avant de corriger les coeff de IQ-Correction
                 //*********************************************************************************************
                 //*****************************  SPLIT MODE ***************************************************
                 //*********************************************************************************************

    if ( is_splitmode() )                  // is SPLIT mode active?
    {
        uint8_t              vfo_rx,           vfo_tx ;
        if (is_vfo_b())    { vfo_rx = VFO_B;   vfo_tx = VFO_A; }
        else       		   { vfo_rx = VFO_A;   vfo_tx = VFO_B; }

        if( txrx_requested_mode == TRX_MODE_TX)                                // are we in TX mode?
        {
            if(ts.txrx_mode == TRX_MODE_RX)                         // did we want to enter TX mode?
            {
                vfo[vfo_rx].band[ts.band].dial_value = df.tune_new; // yes - save current RX frequency in RX VFO location
            }
            tune_new = vfo[vfo_tx].band[ts.band].dial_value;        // load with TX VFO frequency
        }
        else                    // we are in RX mode
        {
            tune_new = vfo[vfo_rx].band[ts.band].dial_value;        // load with RX VFO frequency
        }
    }
    else
    {
         tune_new = df.tune_new;   // we just take the current one if not in split mode
    }

              //****************************************************************************************

     ts.dsp_inhibit  =  1;  // elle sera mis a false par la fonction TimeScheduler


  //	if ( ts.dmod_mode               != DEMOD_CW ) { NR_RAZ_Buffers (); } // Effacer les buffers du NR pour eviter le son burst en debut du Rx

	ts.audio_dac_output_muting_buffer_count  = 5;     //  2 wait at least 2 buffer cycles
	ts.audio_processor_input_mute_counter    = 12;
//	ts.audio_dac_output_muting_flag          =  true;  //  let the audio being muted initially as long as we need it





    if ( txrx_requested_mode == TRX_MODE_TX )
    {
        			// FIXME: Not very robust code, make sure Validate always returns TUNE_IMPOSSIBLE in case of issues
        tx_ok = ( RadioManagement_ValidateFrequencyForTX ( tune_new) != OSC_TUNE_IMPOSSIBLE );


					// this code handles the ts.tx_disable
					// even if ts.tx_disble is set in CW and only in CW we still switch to TX
					// but leave the PA disabled. This is for support of CW training right with the mcHF.
        if ( RadioManagement_IsTxDisabled() || ts.cw_text_entry )
        {
            if ((tx_ok == true && ts.dmod_mode == DEMOD_CW) || ts.cw_text_entry )   {  tx_pa_disabled = true;  }
            else           															{  tx_ok          = false; }
        }
        if ( is_demod_psk() )  { Bpsk_ModulatorInit(); }    // in any other case, it is not okay to transmit with ts.tx_disable == true
    }
    	    //***************************************************************************************************
            //**********************   Permission de bande avant de passer en tX  *******************************
            //***************************************************************************************************
    uint8_t    txrx_mode_final = tx_ok?  txrx_requested_mode : TRX_MODE_RX;

     if (txrx_mode_final != ts.txrx_mode || txrx_mode_final == TRX_MODE_RX)  // only switch mode if tx was permitted or rx was requested
    {

        if ( txrx_mode_final == TRX_MODE_RX )
        {
        	// ts.rx_tx_switch_accentuation = true;
            // agc_wdsp.ring_max_avg  *= 0.71;  // ajout de 3db de volume via le AGC lors de commutation vers Rx

										// remember current agc
										// ads.agc_holder = ads.agc_val;

        	if (  (ts.tx_audio_source == TX_AUDIO_MIC ) && ! tune_mode && !is_splitmode() )   //10*4;  // Nizou
			{
				switch ( ts.dmod_mode )
				 {
					case  DEMOD_USB : tune_new -= ( ts.tx_bass_sheeft - 15 );   break;
					case  DEMOD_LSB : tune_new += ( ts.tx_bass_sheeft - 15 );   break;
				 }
			}

        	RadioManagement_DisablePaBias();  //  kill bias to mute the HF output quickly
            RadioManagement_VswrFoldbackReset();    // N96K P0-4: every new TX starts with a cleared foldback state
        }
        else  // si on doit passer en  Tx
        {
    					// We mute the audio BEFORE we activate the PTT.
    					// This is necessary since U3 is switched the instant that we do so,
    					// rerouting audio paths and causing all sorts of disruption including CLICKs and squeaks.
        	if ( ts.dmod_mode != DEMOD_CW )    // rajouté pour accelerer le 1er Dit/Dah du CW , a tester en CW
        	{
                Codec_PrepareTx ( ts.txrx_mode ); //  elle dure > 15ms        //  5ms
                AudioDriver_Tx_Buffer_Delay_RAZ () ;  // effacer le buffer delay Tx Microphone
        	}


        	RadioManagement_Check_Initialise_Tx_decimated_filters ( tune_mode);





							// this is here to allow CW training
							// with ts.tx_disabled on nothing will be transmitted but you can hear the sidetone
            if (tx_pa_disabled == false)
            {

                GPIO_SetBits   ( PTT_CNTR_PIO, PTT_CNTR );  // Set PTT pin to ON
                Board_RedLed   ( LED_STATE_ON           );  // TX
                Board_GreenLed ( LED_STATE_OFF          );
            }

            while ( ts.audio_dac_output_muting_buffer_count > 1 )   { asm("nop"); }


            if ( ( ts.tx_audio_source == TX_AUDIO_MIC ) && ! tune_mode && !is_splitmode() )   // 10*4;
			{
				switch ( ts.dmod_mode )
				 {
					case  DEMOD_USB : tune_new += ( ts.tx_bass_sheeft -15 );   break;
					case  DEMOD_LSB : tune_new -= ( ts.tx_bass_sheeft -15 );   break;
				 }
			}
        }  // Fin du passage en Tx Final






        df.tune_new = tune_new;

        									// Changement frequence du LO SI5351a en Split et en CW mode
        if (  is_splitmode() || (ts.dmod_mode == DEMOD_CW))
        {
        	RadioManagement_ChangeFrequency ( false, df.tune_new,  txrx_mode_final );     // LO SI5351a adjustement
        }

        // if ( ! is_splitmode() )  {  NIZZ_UpdateLcdFreq ( df.tune_new , White,  UFM_LARGE ); }

        // ts.audio_dac_output_muting_flag = true; // let the audio being muted initially as long as we need it


        // there might have been a band change between the modes, make sure to have the power settings fitting the mode

        if ( txrx_mode_final == TRX_MODE_TX )
        {
            uint8_t  tx_band = RadioManagement_GetBand ( tune_new  );
            // if (tx_band == MAX_BANDS) tx_band = ts.band;  // nizou
            if ( ( tx_band != ts.band ) && (tx_band != MAX_BANDS))  // mise � jour de la bande en cours  avant Txing si n�cessaire .
            {
            	uint16_t   vfo_sell = is_vfo_b() ? VFO_B : VFO_A;
            	UiDriver_UpdateBand ( vfo_sell, ts.band, tx_band );
            	df.tune_new = tune_new;   // frequence peut etre changé par freq du updateband par defaut

            }
            RadioManagement_PowerLevelChange   ( tx_band, ts.power_level  );
       	    RadioManagement_SetBandPowerFactor ( tx_band );   // mise a jour des parametres Tx pour chaque Tx.

        }

					// il faudra tester d'elliminer les deux lignes ChangeFrequency et UpdateLcdFreq en meme temps
					// et laisser la boucle while mettre a jour la frequence et son affichage hors cette foncrtion
                    // voir si le retard > 10ms influence la qualite du switch Tx--Rx



        AudioManagement_SetSidetoneForDemodMode ( ts.dmod_mode, txrx_mode_final == TRX_MODE_RX ? false : tune_mode );
        								// make sure the audio is set properly according to txrx and tune modes

        if ( txrx_mode_final == TRX_MODE_RX )
        {
        	Board_RedLed   ( LED_STATE_OFF          );    // TX led OFF
        	Board_GreenLed ( LED_STATE_ON           );    // RX led ON
            while ( ts.audio_dac_output_muting_buffer_count > 1) {   asm("nop");   }  // Min Delay to wait

            GPIO_ResetBits ( PTT_CNTR_PIO, PTT_CNTR );    // Set PTT pin to OFF
        }

        if ( ts.txrx_mode != txrx_mode_final )
        {
        	   ///***********************************************************************************************
        	   /// *************************   SWITCH CODEC  TX / RX  *******************************************
        	   /// **********************************************************************************************
        	   ///
        	   // ts.audio_dac_output_muting_buffer_count = 4; for (uint i=0; i<37000; i++) { asm("nop");  }

				///*****************************************************************************************************************
				///***********   Mute all Codec inputs/outputs : Speaker , ext_Line-out , Line-Inputs , Mic & Mic-Bias. ************
				///*****************************************************************************************************************

        	Codec_SwitchTxRxMode ( txrx_mode_final );

            ///******************************************************************************************************************

            if ( txrx_mode_final == TRX_MODE_RX )  // Juste avant passage en Rx
            {
#ifdef ATTENUATOR_6DB
            	if ( !ts.antenna_attenuator_enable  && ( ts.display->use_spi == true  ) )
				{
					GPIO_SetBits( ATTENUATION_6DB_PIO,  ATTENUATION_6DB );  // inhibate attenuator -6db si besoins
				}
#endif
               // ts.nr_first_time = 1; // Initialiser la fonction DSP pour ne pas buger apres commutation Tx/Rx


                ts.dynamic_tune_activ_counter          = 0;  // NIZZ pour initialiser le Rx sans problem d'affichage de couleur de la frequence
              //ts.audio_processor_input_mute_counter  = 2;  // pour passer � la saisie Rx directement

                // Pour eviter des clics en retour vers RX on fait muter Tx juste avant durant au moins 2 cycles.
                while (ts.audio_processor_input_mute_counter > 1) { __NOP(); } ;
                // attendre Mute Tx durant 2 cycles de 0.666 ms pour nettoyer les residues des signaux Tx
                // dans les differents buffers, dans le output Codec ect .

                // ts.audio_processor_input_mute_counter = 2;  // pour muter 3 cycles en Rx au switch Tx-->Rx

                // ceci fera deux cycles de mute en Tx d'abord
                // puis deux cycle de Mute en Rx pour eviter les clics en audio Tx-->Rx
            }
            else  // juste avant passage en Tx
            {

#ifdef ATTENUATOR_6DB
               	if ( ts.display->use_spi == true  )
				{
               		GPIO_ResetBits( ATTENUATION_6DB_PIO, ATTENUATION_6DB ); // activate attenuator
				}
#endif

               	RadioManagement_SetPaBias();

                uint32_t   dac_mute_time = 0,  dac_mute_time_mode = 0,  input_mute_time_mode = 0; // aka 1.3ms

                while (ts.audio_processor_input_mute_counter > 1) { __NOP(); } ;

                if ( ts.dmod_mode != DEMOD_CW )
                {
                	// calculate expire time for audio muting in interrupts, it is 15 interrupts per 10ms

                	dac_mute_time = ts.txrx_switch_audio_muting_timing * 15;

                    switch ( ts.tx_audio_source )
                    {
						case TX_AUDIO_MIC     :
						case TX_AUDIO_LINEIN_L:
						case TX_AUDIO_LINEIN_R:  input_mute_time_mode = 4 * 15;
												 dac_mute_time_mode   = 4 * 15 + 5 ;    break;	 // Minimum time is 40 ms
                    }
                }

                ts.audio_processor_input_mute_counter  =  input_mute_time_mode;

                dac_mute_time   = ( dac_mute_time   > dac_mute_time_mode   )? dac_mute_time   : dac_mute_time_mode;
                ts.audio_dac_output_muting_buffer_count =  dac_mute_time; // 15 == 10ms
                ts.dsp_inhibit                          =   0; // autoriser les DSP en Tx
            }


         // AudioDriver_Voice_Tx_Filter_RAZ ();    // NIZZ  pour effacer les buffers des filtres communs utilis�s par Tx

            ts.dynamic_tune_activ_counter             =  20;   // NIZZ pour inhiber la mesure du SWR pendant qq ms du Tx (alleas de mesure)
            ts.txrx_mode                              =  txrx_mode_final;
            ts.audio_dac_output_muting_flag           =  false;   // unmute audio output unless timed muting is active
        }
    }

    ts.FFT_stand_by           =  6;       // tempo 50ms avant reprise echantillonnage FFT et affichag sur ecran
    ads.adc_clip              =  0;
    ads.adc_half_clip         =  0;       // false;      // clear "half clip" indicator that tells us that we should decrease gain
    ads.adc_quarter_clip      =  false;   // clear indicator that, if not triggered, indicates that we can increase gain
}

//************************************************************************************************************************************




/*
void   RadioManagement_SwitchTxRx ( uint8_t  txrx_requested_mode,  bool  tune_mode )
{

    uint32_t         tune_new;
    bool             tx_ok          = false;
    bool             tx_pa_disabled = false;

    // ts.last_tuning = 0;                  // prevents transmitting on wrong frequency during "RX bk phases"
    ts.switch_pause   = 1000; // attendre 2000 trames avant de corriger les coeff de IQ-Correction
                 //*********************************************************************************************
                 //*****************************  SPLIT MODE ***************************************************
                 //*********************************************************************************************
    if ( is_splitmode() )                  // is SPLIT mode active?
    {
        uint8_t              vfo_rx,           vfo_tx ;
        if (is_vfo_b())    { vfo_rx = VFO_B;   vfo_tx = VFO_A; }
        else       		   { vfo_rx = VFO_A;   vfo_tx = VFO_B; }

        if( txrx_requested_mode == TRX_MODE_TX)                                // are we in TX mode?
        {
            if(ts.txrx_mode == TRX_MODE_RX)                         // did we want to enter TX mode?
            {
                vfo[vfo_rx].band[ts.band].dial_value = df.tune_new; // yes - save current RX frequency in RX VFO location
            }
            tune_new = vfo[vfo_tx].band[ts.band].dial_value;        // load with TX VFO frequency
        }
        else                    // we are in RX mode
        {
            tune_new = vfo[vfo_rx].band[ts.band].dial_value;        // load with RX VFO frequency
        }
    }
    else
    {
         tune_new = df.tune_new;   // we just take the current one if not in split mode
    }

              //****************************************************************************************

    if ( txrx_requested_mode == TRX_MODE_TX )
    {
        			// FIXME: Not very robust code, make sure Validate always returns TUNE_IMPOSSIBLE in case of issues
        tx_ok = ( RadioManagement_ValidateFrequencyForTX ( tune_new) != OSC_TUNE_IMPOSSIBLE );


					// this code handles the ts.tx_disable
					// even if ts.tx_disble is set in CW and only in CW we still switch to TX
					// but leave the PA disabled. This is for support of CW training right with the mcHF.
        if ( RadioManagement_IsTxDisabled() || ts.cw_text_entry )
        {
            if ((tx_ok == true && ts.dmod_mode == DEMOD_CW) || ts.cw_text_entry )   {  tx_pa_disabled = true;  }
            else           															{  tx_ok          = false; }
        }
        if ( is_demod_psk() )  { Bpsk_ModulatorInit(); }    // in any other case, it is not okay to transmit with ts.tx_disable == true
    }
    	    //***************************************************************************************************
            //**********************   Permission de bande avant de passer en tX  *******************************
            //***************************************************************************************************
    uint8_t    txrx_mode_final = tx_ok?  txrx_requested_mode : TRX_MODE_RX;

     if (txrx_mode_final != ts.txrx_mode || txrx_mode_final == TRX_MODE_RX)  // only switch mode if tx was permitted or rx was requested
    {
        if ( txrx_mode_final != ts.txrx_mode )  // there is in fact a switch happening which may cause audio issues
        {
        	ts.dsp_inhibit                   =  1;  // elle sera mis a false par la fonction TimeScheduler


        //	if ( ts.dmod_mode               != DEMOD_CW ) { NR_RAZ_Buffers (); } // Effacer les buffers du NR pour eviter le son burst en debut du Rx

        	ts.audio_dac_output_muting_buffer_count  =  4;     //  2 wait at least 2 buffer cycles
            ts.audio_dac_output_muting_flag          =  true;  //  let the audio being muted initially as long as we need it




        }

        if ( txrx_mode_final == TRX_MODE_RX )
        {
        	// ts.rx_tx_switch_accentuation = true;
            // agc_wdsp.ring_max_avg  *= 0.71;  // ajout de 3db de volume via le AGC lors de commutation vers Rx

										// remember current agc
										// ads.agc_holder = ads.agc_val;

        	if (  (ts.tx_audio_source == TX_AUDIO_MIC ) && ! tune_mode && !is_splitmode() )   //10*4;  // Nizou
			{
				switch ( ts.dmod_mode )
				 {
					case  DEMOD_USB : tune_new -= ( ts.tx_bass_sheeft - 15 );   break;
					case  DEMOD_LSB : tune_new += ( ts.tx_bass_sheeft - 15 );   break;
				 }
			}
        }
        else  // si on doit passer en  Tx
        {
    					// We mute the audio BEFORE we activate the PTT.
    					// This is necessary since U3 is switched the instant that we do so,
    					// rerouting audio paths and causing all sorts of disruption including CLICKs and squeaks.
        	if ( ts.dmod_mode != DEMOD_CW )    // rajouté pour accelerer le 1er Dit/Dah du CW , a tester en CW
        	{
                Codec_PrepareTx ( ts.txrx_mode ); //  elle dure > 15ms        //  5ms
        	}

        	RadioManagement_Check_Initialise_Tx_decimated_filters ( tune_mode);


            while ( ts.audio_dac_output_muting_buffer_count > 1)   { asm("nop"); }


							// this is here to allow CW training
							// with ts.tx_disabled on nothing will be transmitted but you can hear the sidetone
            if (tx_pa_disabled == false)
            {
                Board_RedLed   ( LED_STATE_ON           );  // TX
                Board_GreenLed ( LED_STATE_OFF          );
                GPIO_SetBits   ( PTT_CNTR_PIO, PTT_CNTR );  // Set PTT pin to ON
            }


            if ( ( ts.tx_audio_source == TX_AUDIO_MIC ) && ! tune_mode && !is_splitmode() )   // 10*4;
			{
				switch ( ts.dmod_mode )
				 {
					case  DEMOD_USB : tune_new += ( ts.tx_bass_sheeft -15 );   break;
					case  DEMOD_LSB : tune_new -= ( ts.tx_bass_sheeft -15 );   break;
				 }
			}
        }  // Fin du passage en Tx Final






        df.tune_new = tune_new;

        									// Changement frequence du LO SI5351a en Split et en CW mode
        if (  is_splitmode() || (ts.dmod_mode == DEMOD_CW))
        {
        	RadioManagement_ChangeFrequency ( false, df.tune_new,  txrx_mode_final );     // LO SI5351a adjustement
        }

        // if ( ! is_splitmode() )  {  NIZZ_UpdateLcdFreq ( df.tune_new , White,  UFM_LARGE ); }

        // ts.audio_dac_output_muting_flag = true; // let the audio being muted initially as long as we need it


        // there might have been a band change between the modes, make sure to have the power settings fitting the mode

        if ( txrx_mode_final == TRX_MODE_TX )
        {
            uint8_t  tx_band = RadioManagement_GetBand ( tune_new  );
            // if (tx_band == MAX_BANDS) tx_band = ts.band;  // nizou
            if ( ( tx_band != ts.band ) && (tx_band != MAX_BANDS))  // mise � jour de la bande en cours  avant Txing si n�cessaire .
            {
            	uint16_t   vfo_sell = is_vfo_b() ? VFO_B : VFO_A;
            	UiDriver_UpdateBand ( vfo_sell, ts.band, tx_band );
            	df.tune_new = tune_new;   // frequence peut etre changé par freq du updateband par defaut

            }
            RadioManagement_PowerLevelChange   ( tx_band, ts.power_level  );
       	    RadioManagement_SetBandPowerFactor ( tx_band );   // mise a jour des parametres Tx pour chaque Tx.

        }

					// il faudra tester d'elliminer les deux lignes ChangeFrequency et UpdateLcdFreq en meme temps
					// et laisser la boucle while mettre a jour la frequence et son affichage hors cette foncrtion
                    // voir si le retard > 10ms influence la qualite du switch Tx--Rx



        AudioManagement_SetSidetoneForDemodMode ( ts.dmod_mode, txrx_mode_final == TRX_MODE_RX ? false : tune_mode );
        								// make sure the audio is set properly according to txrx and tune modes

        if ( txrx_mode_final == TRX_MODE_RX )
        {
            while ( ts.audio_dac_output_muting_buffer_count > 2 ) {   asm("nop");   }  // Min Delay to wait


            RadioManagement_DisablePaBias();  //  kill bias to mute the HF output quickly


            Board_RedLed   ( LED_STATE_OFF          );    // TX led OFF
            Board_GreenLed ( LED_STATE_ON           );    // RX led ON

            GPIO_ResetBits ( PTT_CNTR_PIO, PTT_CNTR );    // Set PTT pin to OFF

        }

        if ( ts.txrx_mode != txrx_mode_final )
        {
        	   ///***********************************************************************************************
        	   /// *************************   SWITCH CODEC  TX / RX  *******************************************
        	   /// **********************************************************************************************
        	   ///
        	   // ts.audio_dac_output_muting_buffer_count = 4; for (uint i=0; i<37000; i++) { asm("nop");  }

				///*****************************************************************************************************************
				///***********   Mute all Codec inputs/outputs : Speaker , ext_Line-out , Line-Inputs , Mic & Mic-Bias. ************
				///*****************************************************************************************************************

        	Codec_SwitchTxRxMode ( txrx_mode_final );

            ///******************************************************************************************************************

            if ( txrx_mode_final == TRX_MODE_RX )  // Juste avant passage en Rx
            {
#ifdef ATTENUATOR_6DB
           // 	if ( !ts.antenna_attenuator_enable  && ( ts.display->use_spi == true  ) )
			//	{
			//		GPIO_SetBits( ATTENUATION_6DB_PIO,  ATTENUATION_6DB );  // inhibate attenuator -6db si besoins
			//	}
#endif
               // ts.nr_first_time = 1; // Initialiser la fonction DSP pour ne pas buger apres commutation Tx/Rx


                ts.dynamic_tune_activ_counter          = 0;  // NIZZ pour initialiser le Rx sans problem d'affichage de couleur de la frequence
              //ts.audio_processor_input_mute_counter  = 2;  // pour passer � la saisie Rx directement

                // Pour eviter des clics en retour vers RX on fait muter Tx juste avant durant au moins 2 cycles.
                while (ts.audio_processor_input_mute_counter) { __NOP(); __NOP(); __NOP(); } ;
                // attendre Mute Tx durant 2 cycles de 0.666 ms pour nettoyer les residues des signaux Tx
                // dans les differents buffers, dans le output Codec ect .

                // ts.audio_processor_input_mute_counter = 2;  // pour muter 3 cycles en Rx au switch Tx-->Rx

                // ceci fera deux cycles de mute en Tx d'abord
                // puis deux cycle de Mute en Rx pour eviter les clics en audio Tx-->Rx
            }
            else  // juste avant passage en Tx
            {
            	RadioManagement_SetPaBias();

#ifdef ATTENUATOR_6DB
            //   	if ( ts.display->use_spi == true  )
			//	{
            //   		GPIO_ResetBits( ATTENUATION_6DB_PIO, ATTENUATION_6DB ); // activate attenuator
			//	}
#endif



                uint32_t   dac_mute_time = 0,  dac_mute_time_mode = 0,  input_mute_time_mode = 0; // aka 1.3ms



                if ( ts.dmod_mode != DEMOD_CW )
                {
                	// calculate expire time for audio muting in interrupts, it is 15 interrupts per 10ms

                	dac_mute_time = ts.txrx_switch_audio_muting_timing * 15;

                    switch ( ts.tx_audio_source )
                    {
						case TX_AUDIO_MIC     :
						case TX_AUDIO_LINEIN_L:
						case TX_AUDIO_LINEIN_R:  input_mute_time_mode = 4 * 15;
												 dac_mute_time_mode   = 4 * 15 + 5 ;    break;	 // Minimum time is 40 ms
                    }
                }

                ts.audio_processor_input_mute_counter  =  input_mute_time_mode;

                dac_mute_time   = ( dac_mute_time   > dac_mute_time_mode   )? dac_mute_time   : dac_mute_time_mode;
                ts.audio_dac_output_muting_buffer_count =  dac_mute_time; // 15 == 10ms
                ts.dsp_inhibit                          =   0; // autoriser les DSP en Tx
            }


         // AudioDriver_Voice_Tx_Filter_RAZ ();    // NIZZ  pour effacer les buffers des filtres communs utilis�s par Tx

            ts.dynamic_tune_activ_counter             =  20;   // NIZZ pour inhiber la mesure du SWR pendant qq ms du Tx (alleas de mesure)
            ts.txrx_mode                              =  txrx_mode_final;
            ts.audio_dac_output_muting_flag           =  false;   // unmute audio output unless timed muting is active
        }
    }

    ts.FFT_stand_by           =  6;       // tempo 50ms avant reprise echantillonnage FFT et affichag sur ecran
    ads.adc_clip              =  0;
    ads.adc_half_clip         =  0;       // false;      // clear "half clip" indicator that tells us that we should decrease gain
    ads.adc_quarter_clip      =  false;   // clear indicator that, if not triggered, indicates that we can increase gain
}

*/



//************************************************************************************************************************************

/*
 * @returns: false -> use USB, true -> use LSB
 */
bool RadioManagement_CalculateCWSidebandMode()
{
    bool retval = false;

    switch ( RadioManagement_CWConfigValueToModeEntry (ts.cw_offset_mode)->sideband_mode )
    {
		case CW_SB_AUTO:                     // For "auto" modes determine if we are above or below threshold frequency
						// if (RadioManagement_SSB_AutoSideBand(df.tune_new) == DEMOD_USB)   // is the current frequency above the USB threshold?
						retval = (df.tune_new <= USB_FREQ_THRESHOLD && RadioManagement_GetBand(df.tune_new) != BAND_MODE_60);
						// is the current frequency below the USB threshold AND is it not 60m? -> LSB
						break;
		case CW_SB_LSB:
						retval = true;
						break;
		case CW_SB_USB:
		default:
						retval = false;
						break;
    }
    return retval;
}


void RadioManagement_ChangeBandFilter(uchar band)
{
    // here the bands are mapped to the hardware functions to enable the proper
    // filter configuration for a given band

    switch(band)
    {
		case BAND_MODE_2200:
		case BAND_MODE_630 :
		case BAND_MODE_160 :
		case BAND_MODE_80  :   Board_SelectLpfBpf(0);  break;

		case BAND_MODE_60  :
		case BAND_MODE_40  :   Board_SelectLpfBpf(1);  break;

		case BAND_MODE_30  :
		case BAND_MODE_20  :   Board_SelectLpfBpf(2);  break;

		case BAND_MODE_17  :
		case BAND_MODE_15  :
		case BAND_MODE_12  :
		case BAND_MODE_10  :
		case BAND_MODE_6   :
		case BAND_MODE_4   :   Board_SelectLpfBpf(3);  break;

		default:  							           break;
    }
    // ts.nr_first_time = 1; // in case of any Bandfilter change restart the NR routine
}



typedef  struct  BandFilterDescriptor
{
    uint32_t  upper;
    uint16_t  filter_band;
    uint16_t  band_mode;
} BandFilterDescriptor;




#define BAND_FILTER_NUM 7

// The descriptor array below has to be ordered from the lowest BPF frequency filter
// to the highest.
static  const  BandFilterDescriptor  bandFilters[BAND_FILTER_NUM] =
{
    { BAND_FILTER_UPPER_160, COUPLING_160M, BAND_MODE_160},
    { BAND_FILTER_UPPER_80,  COUPLING_80M,  BAND_MODE_80 },
    { BAND_FILTER_UPPER_40,  COUPLING_40M,  BAND_MODE_40 },
    { BAND_FILTER_UPPER_20,  COUPLING_20M,  BAND_MODE_20 },
    { BAND_FILTER_UPPER_10,  COUPLING_15M,  BAND_MODE_10 },
    { BAND_FILTER_UPPER_6,   COUPLING_6M,   BAND_MODE_6  }
};



/**
 * @brief Select and activate the correct BPF for the frequency given in @p freq
 *
 *
 * @param freq The frequency to activate the BPF for in Hz
 *
 * @warning  If the frequency given in @p freq is too high for any of the filters, no filter change is executed.
 */
void   RadioManagement_SetHWFiltersForFrequency( ulong  freq )
{
    int idx;
    for (idx = 0; idx < BAND_FILTER_NUM; idx++)
    {
        if(freq < bandFilters[idx].upper)       // are we low enough if frequency for this band filter?
        {
            if(ts.filter_band != bandFilters[idx].filter_band)
            {
                RadioManagement_ChangeBandFilter(bandFilters[idx].band_mode);   // yes - set to desired band configuration
                ts.filter_band = bandFilters[idx].filter_band;
            }
            break;
        }
    }
}

/**
 * @brief shall the stored power factor be interpreted as coarse or fine (4*resolution of coarse)
 * @param freq in Hertz
 */
bool RadioManagement_IsPowerFactorReduce ( uint32_t freq )
{
  //  return (((freq < 8000000) && (ts.flags2 & FLAGS2_LOW_BAND_BIAS_REDUCE))
  //      || ((freq >= 8000000) && (ts.flags2 & FLAGS2_HIGH_BAND_BIAS_REDUCE))) != 0;
	return false;    // power reduction forc�e � "OFF"  pas besoin , on peut programmer chaque puissance � part rapidement via the Pow pad
}


/*
 * Depends on globals: ts.pwr_adj, ts.power_level, df.tune_new, ts.flags2 & FLAGS2_LOW_BAND_BIAS_REDUCE
 * Impacts globals:    ts.tx_power_factor
 */
void RadioManagement_SetBandPowerFactor(uchar band)
{
    float32_t   pf_bandvalue, pf_levelscale;    // used as a holder for percentage of power output scaling

    if (band >= MAX_BANDS)  // if Out of Band
    {
        // N96K P0-5: restore gating by FLAGS1_TX_OUTSIDE_BANDS (audit U4). TX outside the ham
        // bands is already inhibited in RadioManagement_ValidateFrequencyForTX(); keeping the PA
        // drive at zero here is the second barrier (e.g. if a TX path bypasses the check above).
        if ((ts.flags1 & FLAGS1_TX_OUTSIDE_BANDS) == 0)
        {
            pf_bandvalue       = 0;
            ts.current_pwr_adj = 0;
        }
        else        // TX outside bands **very dirty hack**: I use constant for power
        {
			if (ts.power_level == PA_LEVEL_FULL) { pf_bandvalue = ts.pwr_out_of_band_factor;          }
			else                                 { pf_bandvalue = ts.pwr_out_of_band_factor/2.0 +3.0; }   // 38 // 20
			ts.current_pwr_adj = (uint8_t) ts.pwr_out_of_band_factor;  // pour mettre � jour l'affichage du "PWR" en haut 3em pav� de l'ecran
        }
    }
    else
    {
        pf_bandvalue       = (float)ts.pwr_adj[ts.power_level == PA_LEVEL_FULL?ADJ_FULL_POWER:ADJ_5W][band];
        ts.current_pwr_adj = (uint8_t)  pf_bandvalue;   // pour mettre � jour l'affichage du "PWR" en haut 3em pav� de l'ecran
    }



   //  pf_bandvalue      /= RadioManagement_IsPowerFactorReduce ( df.tune_old )? 800 : 200;

    pf_bandvalue      /= 200.0;


    // now rescale to power levels <5 watts, is so-configured
    switch(ts.power_level)
    {
		case    PA_LEVEL_0_5W: 	pf_levelscale = 0.316;   break;     // rescale for 10% of 5 watts (0.5 watts)
		case    PA_LEVEL_1W  : 	pf_levelscale = 0.447;   break;     // rescale for 20% of 5 watts (1.0 watts)
		case    PA_LEVEL_2W  :	pf_levelscale = 0.6324;  break;     // rescale for 40% of 5 watts (2 watts)
		default              :  pf_levelscale = 1;       break;     // 100% is 5 watts or full power!!
    }


    float32_t  power_factor = pf_levelscale * pf_bandvalue;  // rescale this for the actual power level

    // limit hard limit for power factor since it otherwise may overdrive the PA section
    ts.tx_power_factor =   (power_factor > TX_POWER_FACTOR_MAX_INTERNAL) ?  TX_POWER_FACTOR_MAX_INTERNAL : power_factor;

    if(!ts.iq_freq_mode)	// Is translate mode *NOT* active?
    {
          Codec_TxSidetoneSetgain(ts.txrx_mode);				// adjust the sidetone gain
    }
}




/*
 * @brief returns the band according to the given frequency
 * @param freq  frequency to get band for. Unit is Hertz. This value is used without any further adjustments and should be the intended RX/TX frequency and NOT the IQ center frequency
 *
 */


void  RadioManagement_SetDemodMode( uint8_t new_mode, bool CW_adjust )
{
	ts.switch_pause += 120; // pour libérer un peu de mémoire et du processeur avant de changer de mode
	                        // pour eviter ainsi un plantage quand on passe au mode CW

	ts.audio_dac_output_muting_buffer_count += 50;   // muter le haut parleur Rx audio processing en cours

	//  for (int aaa=0; aaa<10000; aaa++) { }; // delay pour faire passer le Rxprocessing en cours  avant de changer le mode et les coeff des filtres

	DigiModes_TxBufferReset();

	ads.af_disabled++;
    ts.dsp_inhibit++;


    if (new_mode == DEMOD_DIGI)
    {
        if (ts.digital_mode == DigitalMode_None)
        {
            ts.digital_mode = 1;
            // this maps to the first available digital mode (if any)
        }
        RadioManagement_ChangeCodec ( ts.digital_mode, 1 );
    }
    else if (ts.dmod_mode == DEMOD_DIGI)
    {
            RadioManagement_ChangeCodec( ts.digital_mode, 0 );
    }

    if (new_mode == DEMOD_FM && ts.dmod_mode != DEMOD_FM)
    {
        // ads.fm_squelched = true;
        // ads.fm_sql_avg   = 1;
    }

  //  if (ts.cw_offset_shift_keep_signal == true)  //  && (ts.cw_offset_mode == CW_OFFSET_AUTO_SHIFT || ts.cw_offset_mode == CW_OFFSET_LSB_SHIFT || ts.cw_offset_mode == CW_OFFSET_USB_SHIFT))

    if ( CW_adjust )
    {
        // static  int16_t sidetone_mult = 0;

        if ( new_mode == DEMOD_CW && ts.dmod_mode != DEMOD_CW )
        {
            // we come from a non-CW mode
            //if (RadioManagement_UsesBothSidebands(ts.dmod_mode) == false)
            {
                // sidetone_mult = (RadioManagement_LSBActive(ts.dmod_mode)?-1:1);
                // if we have a sideband mode
                // adjust dial frequency by side tone offset
                // if the sidetone frequency is not change, we return exactly to the frequency we have been before
                // this is important if we just cycle through the modes.
                // df.tune_new += sidetone_mult * ts.cw_sidetone_freq;

                 df.tune_new +=  ts.cw_sidetone_freq;
            }
          /*  else
            {
                sidetone_mult = 0;
            }*/
        }


        if (new_mode != DEMOD_CW && ts.dmod_mode == DEMOD_CW)
        {
             // we revert now our frequency change
             // we go to a non-CW mode
             // adjust dial frequency by former side tone offset

             //df.tune_new -= sidetone_mult * ts.cw_sidetone_freq;

        	   df.tune_new -=  ts.cw_sidetone_freq;
         }
    }

    if ((new_mode == DEMOD_CW) && (ts.filter_path < 5 ))  ts.filter_path = 0x7; // NIZZ ajoutee pour forcer le filtre du mode CW lors de changement de mode

    AudioDriver_SetRxAudioProcessing        ( new_mode, false);
    AudioDriver_Voice_Tx_Filter_Init        ( new_mode       );
    AudioManagement_SetSidetoneForDemodMode ( new_mode, false);

    ts.dmod_mode = new_mode;

    ts.nr_first_time = 1; // re-initialize spectral noise reduction, when dmod_mode was changed
    Board_RedLed(LED_STATE_OFF);  // pour eteidre la Led rouge apres mode CW

    // changement automatique du zoom en fonction du mode lors de son changement
    if (new_mode != DEMOD_CW)
    {
    	if ( sd.magnify != 0 )  // Zoom x1
    	{
    		sd.magnify = 0;
    		UiDriver_SpectrumChangeLayoutParameters();
    		UiDriver_Display_spectrum_ZOOM() ;
        }

    	//****** Mise du tune Step automatiquement a 5Hz en mode SSB ************

    	if (!(ts.reverberation_enable & 0x2))   // si le STEP automatic-enable est activé
    	{
			if ((new_mode == DEMOD_USB) || (new_mode == DEMOD_LSB) )
			{
				df.tuning_step	= tune_steps[1];  // 5Hz
				Gett_Tune_ordre_integer(df.tuning_step);
				df.selected_idx = 1;
				UiDriver_DisplayFreqStepSize();  // Update step on screen
			}
    	}
    }
    else
    {
    	if ( sd.magnify != 2 )  //Zoom x8
		{
			sd.magnify = 2;
			UiDriver_SpectrumChangeLayoutParameters();
			UiDriver_Display_spectrum_ZOOM() ;
		}


    	//******* mise tune Step a 1Hz automatiquement **************************//

    	if (!(ts.reverberation_enable & 0x2))   // si le STEP automatic est activé
    	{
			df.tuning_step	= tune_steps[1]; // 5Hz
			Gett_Tune_ordre_integer(df.tuning_step);
			df.selected_idx = 0;
			UiDriver_DisplayFreqStepSize();  // Update step on screen
    	}
    }

    if  ( ts.dsp_inhibit  ) {  ts.dsp_inhibit-- ; }
    if  ( ads.af_disabled ) {  ads.af_disabled--; }
}

//***********************************************************************************


/**
 * @param freq the frequency
 */
bool RadioManagement_FreqIsInBand(const BandInfo* bandinfo, const uint32_t freq)
{
    return (freq >= bandinfo->tune) && (freq <= (bandinfo->tune + bandinfo->size));
}

//*******************************************************************************************

uint8_t RadioManagement_GetBand(uint32_t freq)
{
    static uint8_t   band_scan_old =  99;
    uchar            band_scan;

    band_scan = 0;



    						// first try the last band, and see if it is an match
    if (band_scan_old != 99 && freq >= bandInfo[band_scan_old].tune && freq <= (bandInfo[band_scan_old].tune + bandInfo[band_scan_old].size))
    {
        band_scan = band_scan_old;
    }
    else
    {
        for(band_scan = 0; band_scan < MAX_BANDS; band_scan++)
        {
            if(RadioManagement_FreqIsInBand(&bandInfo[band_scan],freq))   // Is this frequency within this band?
            {
                break;  // yes - stop the scan
            }
        }
    }
    band_scan_old = band_scan;  // update band change detector
    return band_scan;       // return with the band
}


//***************************************************************************************************
//***************************************************************************************************************




uint32_t  RadioManagement_SSB_AutoSideBand ( uint32_t freq )
{
    uint32_t retval;

    if((ts.lsb_usb_auto_select == AUTO_LSB_USB_60M) && ((freq < USB_FREQ_THRESHOLD) && (RadioManagement_GetBand(freq) != BAND_MODE_60)))       // are we <10 MHz and NOT on 60 meters?
    {
        retval = DEMOD_LSB;
    }
    else if((ts.lsb_usb_auto_select == AUTO_LSB_USB_ON) && (freq < USB_FREQ_THRESHOLD))      // are we <10 MHz (not in 60 meter mode)
    {
        retval = DEMOD_LSB;
    }
    else        // we must be > 10 MHz OR on 60 meters
    {
        retval = DEMOD_USB;
    }
    return retval;
}


const  int32_t  ptt_debounce_time =  3;  // 4; //Nizou 3 // n*10ms, delay for debouncing manually started TX (using the PTT button / input line)

#define PTT_BREAK_IDLE (-1)

//***********************************************************************************************************

void   RadioManagement_HandlePttOnOff()
{
    static  int64_t    ptt_break_timer = PTT_BREAK_IDLE;  //-1

#ifdef TUNISIAN
   if ( ! RadioManagement_IsTxDisabled() )  // NIZZ pour desactiver le Tx hors bande
#endif
   {

		if ( ts.tune == false)   // not when tuning, in this case we are TXing already anyway until tune is being stopped
		{
			if ( ts.ptt_req )    // we are asked to start TX
			{
				if ( ts.txrx_mode == TRX_MODE_RX && (RadioManagement_IsTxDisabled() == false || ts.dmod_mode == DEMOD_CW || ts.cw_text_entry)) // FIXME cw_text_entry situation is not correctly processed
				{  RadioManagement_SwitchTxRx ( TRX_MODE_TX, false );  }
				ts.tx_immediate_stop_req = false;   // if we have a ptt request, all stop requests are cancelled
				ts.ptt_req               = false;   // the ptt request has been processed
			}
			else if ( CatDriver_CatPttActive() == false )
			{
				if ( ts.tx_immediate_stop_req && is_demod_psk() && !psk_state.tx_ending )   // When CAT driver "pressed" PTT skip auto return to RX
				{
					psk_state.tx_ending       = true;
				 // ts.tx_immediate_stop_req  = false;
				}
				else if ( ts.txrx_mode == TRX_MODE_TX )  // If we are in TX and ...
				{
					if ( !(ts.dmod_mode == DEMOD_CW || is_demod_rtty() || is_demod_psk() || ts.cw_text_entry) || ts.tx_immediate_stop_req )
					{
						if ( Board_PttDahLinePressed() == false )   // ... the PTT line is released ...
						{
							if ( ptt_break_timer == PTT_BREAK_IDLE )          // ... we start the break timer if not started already!
							{
								ptt_break_timer = ts.sysclock + ptt_debounce_time;
							}
						}
						else { ptt_break_timer = PTT_BREAK_IDLE;  }          // ... but if not released we stop the break timer

										// Temporisation de 30ms avant confirmation de retour immediat  vers  Rx.

						if ( ( (ptt_break_timer < ts.sysclock) && (ptt_break_timer != PTT_BREAK_IDLE) ) || ts.tx_immediate_stop_req )
						{
							RadioManagement_SwitchTxRx ( TRX_MODE_RX, false );    // ... go back to RX and ...
							ptt_break_timer = PTT_BREAK_IDLE;                     // ... stop the timer
						}
					}

				}
				ts.tx_immediate_stop_req = false;        // if we are here a stop request has been processed completely
			}
		}
   }
}

//***********************************************************************************************

bool RadioManagement_IsApplicableDemodMode( uint32_t  demod_mode )
{
    bool  retval = false;
    switch( demod_mode )
    {
		case DEMOD_LSB:
		case DEMOD_USB:	if((ts.lsb_usb_auto_select))       // is auto-select LSB/USB mode enabled AND mode-skip NOT enabled?
						{
							   retval = RadioManagement_SSB_AutoSideBand(df.tune_new ) == demod_mode;       // is this a voice mode, subject to "auto" LSB/USB select?
						}
						else { retval = true;}
						break;

		case DEMOD_AM:	retval = (ts.demod_mode_disable & DEMOD_AM_DISABLE) == 0;      // is AM enabled?
						break;

		case DEMOD_DIGI:retval = (ts.demod_mode_disable & DEMOD_DIGI_DISABLE) == 0;      // is DIGI enabled?
						if((ts.lsb_usb_auto_select) && retval == true)       // is auto-select LSB/USB mode enabled AND mode-skip NOT enabled?
						{
							// TODO: this is only true for FreeDV, but since we have only FreeDV...
							ts.digi_lsb = RadioManagement_SSB_AutoSideBand(df.tune_new ) == DEMOD_LSB;
							// is this a voice mode, subject to "auto" LSB/USB select?
						}
						break;

		case DEMOD_CW:	retval = (ts.demod_mode_disable & DEMOD_CW_DISABLE) == 0;      // is CW enabled?
						break;

		case DEMOD_FM:	// FIXME: ts.lsb_usb_auto_select acts as fm select here. Rename!
						retval = (ts.iq_freq_mode != FREQ_IQ_CONV_MODE_OFF) && (((ts.flags2 & FLAGS2_FM_MODE_ENABLE) != 0) || (ts.band == BAND_MODE_10 && ts.lsb_usb_auto_select));   // is FM enabled?
						break;

		case DEMOD_SAM:	retval =( ts.flags1 & FLAGS1_SAM_ENABLE) != 0;        // is SAM enabled?
						break;


		default:		retval = true;
    }
    return   retval;
}

/**
 * @brief finds next related alternative modes for a given mode (e.g. for USB it returns  LSB)
 * @returns next mode OR same mode if none found.
 */
uint32_t  RadioManagement_NextAlternativeDemodMode( uint32_t  loc_mode )
{
    uint32_t  retval = loc_mode;
    // default is to simply return the original mode
    switch( loc_mode )
    {
		case DEMOD_USB:	retval = DEMOD_LSB;  	break;

		case DEMOD_LSB:	retval = DEMOD_USB;		break;

		case DEMOD_CW:	// FIXME: get rid of ts.cw_lsb
						// better use it generally to indicate selected side band (also in SSB)
						ts.cw_lsb  =  true;    break;    // !ts.cw_lsb;


		case DEMOD_AM:	retval = DEMOD_SAM;
						break;

		case DEMOD_SAM:	(uint8_t) ads.sam_sideband++;
						// stereo SAM is only switchable if you have stereo modes enabled
						uint8_t   minus = 0;
						if (ads.sam_sideband > (SAM_SIDEBAND_MAX - minus))
						{
							ads.sam_sideband = SAM_SIDEBAND_BOTH;
							retval           = DEMOD_AM;
						}
						break;

		case DEMOD_DIGI: ts.digi_lsb = !ts.digi_lsb;  	break;

		case DEMOD_FM:	// toggle between narrow and wide fm
						RadioManagement_FmDevSet5khz( !RadioManagement_FmDevIs5khz() );
    }
    // if there is no explicit alternative mode
    // we return the original mode.
    return retval;
}

/**
 * @brief find the next "different" demodulation mode which is enabled.
 * @returns new mode, or current mode if no other mode is available
 */
uint32_t  RadioManagement_NextNormalDemodMode( uint32_t  loc_mode )
{
    uint32_t  retval = loc_mode;
    // default is to simply return the original mode
    do {
        retval++;
        if ( retval > DEMOD_MAX_MODE )
        {
            retval = 0;
            // wrap around;
        }
    }    while ( RadioManagement_IsApplicableDemodMode(retval) == false && retval != loc_mode );
    // if we loop around to the initial mode, there is no other option than the original mode
    // so we return it, otherwise we provide the new mode.

    return retval;
}

//************************************************************************************************

bool RadioManagement_UsesBothSidebands(uint16_t dmod_mode)
{
    bool  retval =
            (
                    (   dmod_mode == DEMOD_AM)
                    ||( dmod_mode == DEMOD_SAM && (ads.sam_sideband == SAM_SIDEBAND_BOTH))
                    || (dmod_mode == DEMOD_FM)
            );
    return  retval;
}

bool  RadioManagement_LSBActive( uint16_t  dmod_mode )
{
    bool    is_lsb;

    switch( dmod_mode )        // determine if the receiver is set to LSB or USB or FM
    {
		case DEMOD_SAM: is_lsb = ads.sam_sideband == SAM_SIDEBAND_LSB;		break;
		case DEMOD_LSB:	is_lsb = true;                                      break;  // it is LSB
		case DEMOD_CW:	is_lsb = ts.cw_lsb;                                 break;   // is this USB RX mode?  (LSB of mode byte was zero)
		case DEMOD_DIGI:is_lsb = ts.digi_lsb;								break;
		case DEMOD_USB:
		default:		is_lsb = false;                                     break;   // it is USB
    }

    return  is_lsb;
}

bool  RadioManagement_USBActive( uint16_t  dmod_mode )
{
    return  RadioManagement_UsesBothSidebands( dmod_mode ) == false && RadioManagement_LSBActive( dmod_mode ) == false;
}


static void RadioManagement_PowerFromADCValue( float val, float sensor_null, float coupling_calc, volatile float* pwr_ptr, volatile float* dbm_ptr)
{
    float  pwr;
    float  dbm;
    val *= SWR_ADC_VOLT_REFERENCE;    // get nominal A/D reference voltage
    val /= SWR_ADC_FULL_SCALE;        // divide by full-scale A/D count to yield actual input voltage from detector
    val += sensor_null;               // offset result

    if(val <= LOW_POWER_CALC_THRESHOLD)     // is this low power as evidenced by low voltage from the sensor?
    {
        pwr = LOW_RF_PWR_COEFF_A + (LOW_RF_PWR_COEFF_B * val) + (LOW_RF_PWR_COEFF_C * powf(val,2 )) + (LOW_RF_PWR_COEFF_D * powf(val, 3));
    }
    else            // it is high power
    {
        pwr = HIGH_RF_PWR_COEFF_A + (HIGH_RF_PWR_COEFF_B * val) + (HIGH_RF_PWR_COEFF_C * powf(val, 2));
    }
    // calculate forward and reverse RF power in watts (p = a + bx + cx^2) for high power (above 50-60

    if(pwr < 0)     // prevent negative power readings from emerging from the equations - particularly at zero output power
    {
        pwr = 0;
    }

    dbm       =  (10 * (log10f(pwr))) + 30 + coupling_calc;
    pwr       =  pow10f(dbm/10)/1000;
    *pwr_ptr  =  pwr;
    *dbm_ptr  =  dbm;
}

/*
 * N96K P0-4: VSWR foldback protection for the PA (audit U3, adapted from upstream 2.12.2 dcbebe5f).
 * N96K P1-3: the protection no longer depends on the TX display settle counter (audit R1/R5). It uses
 * its own counters of valid measurements, keeps the PA bias off and the drive at the minimum while
 * folded back and restores the user power only after a confirmed recovery at minimal drive, so full
 * power is never applied into a bad load (audit R3).
 * All power/bias changes are done while transmitting and only when no TX/RX transition is pending.
 */
static bool     vswr_foldback_active    = false;    // foldback engaged: PA bias off, drive minimal
static bool     vswr_foldback_trial     = false;    // recovery trial: PA bias on, drive minimal, measuring
static uint8_t  vswr_foldback_retry     = 0;        // measurements since the last recovery trial
static uint8_t  vswr_foldback_recovery  = 0;        // consecutive good measurements during a recovery trial
static uint8_t  vswr_settle_cnt         = 0;        // 10 ms measurement ticks since the TX start (settle window)
static float    vswr_foldback_avg       = 0.0f;     // dampened VSWR value

/**
 * @brief N96K P0-4: reduce the TX power factor to the minimal level. The user's power level
 * (ts.power_level, stored in EEPROM) is not modified, only the TX gain factor is changed.
 */
static void RadioManagement_SetMinimalTxPowerFactor()
{
    uint8_t  band        = RadioManagement_GetBand(df.tune_new);
    uint8_t  saved_level = ts.power_level;

    ts.power_level = PA_LEVEL_MINIMAL;
    RadioManagement_SetBandPowerFactor(band);
    ts.power_level = saved_level;
}

/**
 * @brief N96K P0-4: release the VSWR foldback, restore the PA bias and the user's power factor.
 * N96K P1-3: called only for a confirmed recovery (N good measurements at minimal drive) or if the
 * protection has been disabled at run time, so full power is never restored while the load is bad.
 */
static void RadioManagement_VswrFoldbackRelease()
{
    vswr_foldback_active    = false;
    vswr_foldback_trial     = false;
    vswr_foldback_retry     = 0;
    vswr_foldback_recovery  = 0;
    vswr_foldback_avg       = 0.0f;
    swrm.high_vswr_detected = false;

    // restore the TX power factor of the user's power level
    RadioManagement_SetBandPowerFactor(RadioManagement_GetBand(df.tune_new));

    if (ts.txrx_mode == TRX_MODE_TX)    // give the PA its bias back (it was killed by the foldback)
    {
        RadioManagement_SetPaBias();
    }
}

/**
 * @brief N96K P0-4: clear the VSWR foldback state when returning to RX.
 * N96K P1-3: also clears the measurement counters, so every new TX starts with a fresh settle window.
 */
static void RadioManagement_VswrFoldbackReset()
{
    vswr_foldback_active    = false;
    vswr_foldback_trial     = false;
    vswr_foldback_retry     = 0;
    vswr_foldback_recovery  = 0;
    vswr_settle_cnt         = 0;
    vswr_foldback_avg       = 0.0f;
    swrm.high_vswr_detected = false;
}

/**
 * @brief N96K P0-4: fold the PA back if the transmitted VSWR exceeds the threshold.
 * N96K P1-3: state machine (audit R1/R3/R5):
 *   normal -> (ARM_CNT measurement ticks, ~30 ms) -> trip on dampened VSWR > threshold
 *   foldback: PA bias off + minimal drive, held every measurement (a PWR change by the user is
 *             overridden, so the PA never gets full drive while folded back)
 *   every RETRY_CNT measurements one recovery trial: PA bias on but MINIMAL drive, so a bad load
 *             is measured at safe power only; a bad trial mutes the PA again
 *   RECOVERY_CNT consecutive good measurements below (threshold - hysteresis) -> release: restore the
 *             user power and the PA bias
 * The dampened value uses fast attack / slow decay, so the PA does not oscillate.
 * Must be called after a power measurement only.
 */
static void RadioManagement_CheckVswrProtection()
{
    if (ts.txrx_mode != TRX_MODE_TX)            { return; }
    if (ts.ptt_req || ts.tx_immediate_stop_req) { return; }   // never switch power on a TX/RX transition

    if (ts.vswr_protection_threshold <= VSWR_PROTECTION_THRESHOLD_OFF)  // protection disabled at run time
    {
        if (vswr_foldback_active)
        {
            RadioManagement_VswrFoldbackRelease();  // do not leave the PA muted when the user switches it off
        }
        return;
    }

    // N96K P1-3: own settle window counted in 10 ms measurement ticks (audit R1) - neither the TX
    // display settle counter nor any tuning action may suppress the protection (audit R5).
    if (vswr_settle_cnt < VSWR_PROTECTION_ARM_CNT)
    {
        return;
    }

    bool  valid_vswr = (swrm.fwd_pwr >= SWR_MIN_CALC_POWER);  // enough forward power for a valid VSWR measurement

    if (valid_vswr)
    {
        // fast attack, slow decay, the hysteresis is applied on this dampened value
        if ((vswr_foldback_avg < 1.0f) || (swrm.vswr > vswr_foldback_avg))
        {
            vswr_foldback_avg = swrm.vswr;
        }
        else
        {
            vswr_foldback_avg = vswr_foldback_avg * (1 - VSWR_DAMPENING_FACTOR) + swrm.vswr * VSWR_DAMPENING_FACTOR;
        }
    }

    if (vswr_foldback_active == false)
    {
        if (valid_vswr == false)
        {
            return;                                 // nothing to protect against (yet)
        }
        if (vswr_foldback_avg > (float)ts.vswr_protection_threshold)
        {
            RadioManagement_DisablePaBias();            // mute the PA immediately
            RadioManagement_SetMinimalTxPowerFactor();  // and reduce the drive to the minimum
            swrm.high_vswr_detected = true;
            vswr_foldback_active    = true;
            vswr_foldback_trial     = false;
            vswr_foldback_retry     = 0;
            vswr_foldback_recovery  = 0;
        }
        return;
    }

    // ---------------- foldback is active ----------------
    // Keep the drive at the minimum in every foldback state: a power level change by the user while
    // transmitting must not be able to re-apply full drive (audit R3).
    RadioManagement_SetMinimalTxPowerFactor();

    if (vswr_foldback_trial == false)
    {
        RadioManagement_DisablePaBias();            // hold the PA muted while waiting for a recovery trial
        if (++vswr_foldback_retry < VSWR_PROTECTION_RETRY_CNT)
        {
            return;
        }
        // start a recovery trial: the PA bias comes back but the drive stays minimal, so a bad load
        // is only measured at safe power (no burst of full power, audit R3)
        vswr_foldback_retry = 0;
        vswr_foldback_avg   = 0.0f;                 // first trial reading initializes the integrator
        vswr_foldback_trial = true;
        RadioManagement_SetMinimalTxPowerFactor();
        RadioManagement_SetPaBias();
        return;
    }

    if (valid_vswr == false)                        // the trial did not produce a valid reading
    {
        if (++vswr_foldback_retry >= VSWR_PROTECTION_RETRY_CNT)
        {
            vswr_foldback_trial = false;            // abort the trial, mute the PA and try again later
            vswr_foldback_retry = 0;
            RadioManagement_DisablePaBias();
        }
        return;
    }

    if (vswr_foldback_avg > (float)ts.vswr_protection_threshold)
    {
        // still a bad load: mute the PA again, the next trial follows after VSWR_PROTECTION_RETRY_CNT
        RadioManagement_DisablePaBias();
        vswr_foldback_trial     = false;
        vswr_foldback_retry     = 0;
        vswr_foldback_recovery  = 0;
        return;
    }

    if (vswr_foldback_avg < ((float)ts.vswr_protection_threshold - VSWR_PROTECTION_HYSTERESIS))
    {
        if (++vswr_foldback_recovery >= VSWR_PROTECTION_RECOVERY_CNT)
        {
            RadioManagement_VswrFoldbackRelease();  // load is good again: restore bias and user power
        }
        return;
    }

    vswr_foldback_recovery = 0;                     // inside the hysteresis band: keep the trial running
}


/*
 * @brief Measures and calculates TX Power Output and SWR, has to be called regularly
 * @returns true if new values have been calculated
 */
bool  RadioManagement_UpdatePowerAndVSWR()
{

    uint16_t   val_p,val_s = 0;
    float      sensor_null, coupling_calc;
    bool       retval      = false;

    // Collect samples
    if(swrm.p_curr < SWR_SAMPLES_CNT)
    {
        // Get next sample
        if(!(ts.flags1 & FLAGS1_SWAP_FWDREV_SENSE))       // is bit NOT set?  If this is so, do NOT swap FWD/REV inputs from power detectors
        {
            val_p = HAL_ADC_GetValue(&hadc2); // forward
            val_s = HAL_ADC_GetValue(&hadc3); // return
        }
        else        // FWD/REV bits should be swapped
        {
            val_p = HAL_ADC_GetValue(&hadc3); // forward
            val_s = HAL_ADC_GetValue(&hadc2); // return
        }

        // Add to accumulator to average A/D values
        swrm.fwd_calc += val_p;
        swrm.rev_calc += val_s;

        swrm.p_curr++;

        // N96K P1-3: count the measurement ticks of the current TX for the VSWR protection settle window
        if (vswr_settle_cnt < 255)
        {
            vswr_settle_cnt++;
        }
    }
    else
    {
        // obtain and calculate power meter coupling coefficients
        coupling_calc = ( swrm.coupling_calc[ts.filter_band] - 100.0)/10.0;
        // offset to zero and rescale to 0.1 dB/unit


        sensor_null = ( swrm.sensor_null - 100.0 ) / 1000 ;
        // get calibration factor
        // offset it so that 100 = 0
        // divide so that each step = 1 millivolt

        // Compute average values
        RadioManagement_PowerFromADCValue ( swrm.fwd_calc / SWR_SAMPLES_CNT,  sensor_null,  coupling_calc,  &swrm.fwd_pwr,  &swrm.fwd_dbm );
        RadioManagement_PowerFromADCValue ( swrm.rev_calc / SWR_SAMPLES_CNT,  sensor_null,  coupling_calc,  &swrm.rev_pwr,  &swrm.rev_dbm );

        // Reset accumulators and variables for power measurements
        swrm.p_curr   = 0;
        swrm.fwd_calc = 0;
        swrm.rev_calc = 0;
        // N96K P0-4: calculate VSWR only if the forward power is high enough for a valid measurement
        // (audit U3). Otherwise keep the previous value: no division by zero, no NaN and no negative
        // VSWR when the reverse reading happens to be higher than the forward one.
        if (swrm.fwd_pwr >= SWR_MIN_CALC_POWER)
        {
            float  vswr_ratio = swrm.rev_pwr / swrm.fwd_pwr;    // reflected / forward power

            if (!(vswr_ratio >= 0.0f))                          // also catches NaN
            {
                vswr_ratio = 0.0f;
            }
            if (vswr_ratio > 1.0f)                              // full reflection (or detector offset)
            {
                vswr_ratio = 1.0f;
            }

            if (vswr_ratio > 0.9801f)                           // 0.99^2: (almost) infinite mismatch
            {
                swrm.vswr = VSWR_MAX_CALC_VALUE;
            }
            else
            {
                float  vswr_refl = sqrtf(vswr_ratio);           // reflection coefficient
                swrm.vswr = (1.0f + vswr_refl) / (1.0f - vswr_refl);
            }
        }

        RadioManagement_CheckVswrProtection();
        retval = true;
    }
    return retval;
}

/**
 * @brief Calculation and setting of RX Audio Codec gain for ADC of IQ signal and detects signal clipping, needs to be called regularly every 40ms.
 */



void RadioManagement_HandleRxIQSignalCodecGain()     // chaque 40 ms  seulement en mode Rx
{
    static  uint16_t    rfg_timer= 0;   // counter used for timing RFG control decay
    static  float       auto_rfg = 8;
    float               rfg_calc;
    float               gcalc;



    						// Now handle automatic A/D input gain control timing
    rfg_timer++; if ( rfg_timer > 10000 ) { rfg_timer = 10000; }  // limit count of RFG timer

    if(ads.adc_half_clip > 150)       // did clipping almost occur?  150 / 960
    {
        if(rfg_timer >= 9 )   // AUTO_RFG_DECREASE_LOCKOUT)      // has enough time passed since the last gain decrease?
        {
            if(auto_rfg)        // yes - is this NOT zero?
            {
                auto_rfg -= 0.25;    // decrease gain one step, 1.5dB (it is multiplied by 2, above)
                rfg_timer = 0;       // reset the adjustment timer
            }
        }
    }
    else if (!ads.adc_quarter_clip)      // no clipping occurred
    {
        if(rfg_timer >= 12)  // AUTO_RFG_INCREASE_TIMER)        // has it been long enough since the last increase?
        {
            auto_rfg   += 0.25;  if( auto_rfg > 8)  { auto_rfg = 8; }  // limit to 8 // increase gain by one step, 1.5dB (it is multiplied by 2, above)
            rfg_timer   = 0;  // reset the timer to prevent this from executing too often
        }
    }


    ads.adc_half_clip    = 0;       // false;      // clear "half clip" indicator that tells us that we should decrease gain
    ads.adc_quarter_clip = false;   // clear indicator that, if not triggered, indicates that we can increase gain


				// ************************
				// Update S-Meter and control the input gain of the codec to maximize A/D and receiver dynamic range
				// ************************
				//
				// Calculate attenuation of "RF Codec Gain" setting so that S-meter reading can be compensated.
				// for input RF attenuation setting
				//

        if(ts.rf_codec_gain == RF_CODEC_GAIN_AUTO)      // Is RF gain in "AUTO" mode?
        {
            rfg_calc = auto_rfg;
        }
        else                    // not in "AUTO" mode
        {
            rfg_calc  = (float)ts.rf_codec_gain;     // get copy of RF gain setting
            auto_rfg  = rfg_calc;        // keep "auto" variable updated with manual setting when in manual mode
            rfg_timer = 0;
        }

     // rfg_calc  = (auto_rfg + 1) * 2 + 13; --> 9 * 2 + 13 = 31 !
     // rfg_calc += 1;  // offset to prevent zero
        rfg_calc *= 4;    // 2 double the range of adjustment
        rfg_calc -= 1;    // 13; // offset, as bottom of range of A/D gain control is not useful (e.g. ADC saturates before RX hardware)
        if       ( rfg_calc > 31)  { rfg_calc = 31; }    // limit calc to hardware range
        else if  ( rfg_calc <  0)  { rfg_calc =  0; }

        if (( ts.old_rfg_calc != (uint8_t) rfg_calc) && ts.enable_display_on_screen ) // set the RX gain on the codec
        {

        	if ((rfg_calc < 4) && (ts.rf_codec_gain == RF_CODEC_GAIN_AUTO )) { Codec_RestartI2S();          auto_rfg = 8;}
        	else                                                             { Codec_IQInGainAdj ( (uint8_t) rfg_calc ); }

        	ts.old_rfg_calc =   (uint8_t) rfg_calc;

        	///***********  Affichage sur ecran  du Gain Codec en dB de 0..-31dB ****************
        	if ( ! ts.show_debug_info )
        	{
        		uint32_t color = White; if (ts.antenna_attenuator_enable)  color = Green;
				char txt [ 12 ];
				snprintf ( txt, 12, "%3ddB", -31 + (int) ts.old_rfg_calc - (6*ts.antenna_attenuator_enable));
				UiLcdHy28_PrintText ( ts.Layout->DisplayDbm.x,   ts.Layout->DisplayDbm.y, txt, color, Black, 0 );
        	}
        }

        // Now calculate the RF gain setting
        gcalc = pow10(((rfg_calc * 1.5) - 34.5) / 10) ;

        // codec has 1.5 dB/step
        // offset codec setting by 34.5db (full gain = 12dB)
        // convert to power ratio

        ads.codec_gain_calc = sqrtf(gcalc);     // convert to voltage ratio - we now have current A/D (codec) gain setting
}
///************************************************************************************************************************

const cw_mode_map_entry_t cw_mode_map[] =
{
        {CW_OFFSET_TX   , CW_SB_USB },  // 0
        {CW_OFFSET_TX   , CW_SB_LSB },  // 1
        {CW_OFFSET_TX   , CW_SB_AUTO},  // 2
        {CW_OFFSET_RX   , CW_SB_USB },  // 3
        {CW_OFFSET_RX   , CW_SB_LSB },  // 4
        {CW_OFFSET_RX   , CW_SB_AUTO},  // 5
        {CW_OFFSET_SHIFT, CW_SB_USB },  // 6
        {CW_OFFSET_SHIFT, CW_SB_LSB },  // 7
        {CW_OFFSET_SHIFT, CW_SB_AUTO},  // 8
};


const  cw_mode_map_entry_t*  RadioManagement_CWConfigValueToModeEntry( uint8_t  cw_offset_mode )
{
    const  cw_mode_map_entry_t*  retval = &cw_mode_map[0];

    if (cw_offset_mode < CW_OFFSET_NUM)
    {
        retval = &cw_mode_map[cw_offset_mode];
    }
    return  retval;
}




uint8_t  RadioManagement_CWModeEntryToConfigValue( const  cw_mode_map_entry_t*  mode_entry )
{
    uint8_t  retval = CW_OFFSET_MODE_DEFAULT;

    for ( uint8_t cw_offset_mode = 0; cw_offset_mode < CW_OFFSET_NUM; cw_offset_mode++ )
    {
        if (    cw_mode_map[cw_offset_mode].dial_mode     == mode_entry->dial_mode
             && cw_mode_map[cw_offset_mode].sideband_mode == mode_entry->sideband_mode )
        {
            retval = cw_offset_mode;
            break;
        }
    }
    return  retval;
}

void   RadioManagement_ToggleVfoAB()
{
    uint8_t  vfo_new, vfo_active;

    if( is_vfo_b() )          // LSB on VFO mode byte set?
    {
        vfo_new          = VFO_A;
        vfo_active       = VFO_B;
        ts.vfo_mem_mode &= ~VFO_MEM_MODE_VFO_B; // yes, it's now VFO-B mode, so clear it, setting it to VFO A mode
    }
    else                            // LSB on VFO mode byte NOT set?
    {
        ts.vfo_mem_mode |= VFO_MEM_MODE_VFO_B;          // yes, it's now in VFO-A mode, so set it, setting it to VFO B mode
        vfo_new          = VFO_B;
        vfo_active       = VFO_A;
    }
    vfo[vfo_active].band[ts.band].dial_value   = df.tune_old;     // band_dial_value[ts.band];     // save "VFO B" settings
    vfo[vfo_active].band[ts.band].decod_mode   = ts.dmod_mode;    // band_decod_mode[ts.band];
    vfo[vfo_active].band[ts.band].digital_mode = ts.digital_mode;

    df.tune_new = vfo[vfo_new].band[ts.band].dial_value;

    bool digitalModeDiffers = ts.digital_mode != vfo[vfo_new].band[ts.band].digital_mode;
    bool newIsDigitalMode   = vfo[vfo_new].band[ts.band].decod_mode == DEMOD_DIGI;
    ts.digital_mode         = vfo[vfo_new].band[ts.band].digital_mode;

    if(ts.dmod_mode != vfo[vfo_new].band[ts.band].decod_mode || (newIsDigitalMode &&  digitalModeDiffers) )
    {
        RadioManagement_SetDemodMode(vfo[vfo_new].band[ts.band].decod_mode, false );
    }
    ts.nr_first_time = 1; // restart in case of VFO-Toggle
}

/**
 * @returns true == fm deviation 5khz, false == fm deviation = 2.5khz
 */

bool   RadioManagement_FmDevIs5khz()
{
    return ( ts.flags2 & FLAGS2_FM_MODE_DEVIATION_5KHZ ) != 0;
}

/**
 * @param is5khz true == fm deviation 5khz, false == fm deviation = 2.5khz
 */
void   RadioManagement_FmDevSet5khz( bool is5khz )
{
    if ( is5khz )  {  ts.flags2 |=  FLAGS2_FM_MODE_DEVIATION_5KHZ; }
    else           {  ts.flags2 &= ~FLAGS2_FM_MODE_DEVIATION_5KHZ; }
}

