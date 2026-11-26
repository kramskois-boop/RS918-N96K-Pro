
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

#include "filters.h"







// Rx Audio LPF 16.5 KHz IIR filter ;  Elleptic stop band = -75 db  (159 µs max @ 16.270 KHz)  0.00dB ripples // Nizar 15 Fev 2022
// simul� par lowa Hills IIR filter designer  vers 6.5:
// LPF 16.5 Khz  ( 0.34280) ,  Elleptic 7em ordre, stop band = -75 db  (159 µs max @ 16.270 KHz)  (  0.00dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
//  format long
//  a = [  1.000000000000000000,    -2.933278391522126330E0,   4.771425692139010800E0,   -4.828034715756298390E0,  3.274801258983582210E0,  -1.449930771840012510E0,   3.880635854386131900E-1,  -4.799683631378047540E-2 ]
//  b = [  5.252239251453607590E-3,  1.478673299675138390E-2,  2.888846406352720830E-2,   3.859747425276183640E-2, 3.859747425276183640E-2,  2.888846406352720830E-2,  1.478673299675138390E-2,   5.252239251453607590E-3 ]
//  [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (7 coeff)  et v (8 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_16k5_LPF =
{
    .numStages = 7,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.047996836313780,
    	 0.247846465602795,
    	-0.532385521635271,
    	 0.725960763305171,
    	-0.759447366136307,
    	 0.915399703442402,
    	-0.603745938937568
      },

    .pvCoeffs = (float*) (const float[])
    {
    	
    	0.005252239251454,
    	0.030193012900145,
    	0.092033135633826,
    	0.178385387170674,
    	0.203416242206041,
    	0.114112273532793,
    	0.007513256934910,
       -0.027920897940569
      }
};
*/


// Rx Audio LPF 16.5 KHz IIR filter ;  Elleptic stop band = -69 db  (170 µs max @ 16.170 KHz)  0.080dB ripples // Nizar 10 Nov 2022
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 16.5 Khz  ( 0.34460) ,  Elleptic 6em ordre, stop band = -69 db  (170 µs max @ 16.170 KHz)  (  0.080dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,    -2.950976349983362110E0,   4.662541848127230710E0,  -4.469282927811726760E0 , 2.728512601121159340E0,  -9.892756750440494070E-1, 1.675522260485414970E-1 ]
// b = [  7.553559379242526180E-3,  1.776455290340989370E-2,  3.123461510365868500E-2,  3.596626768517114940E-2, 3.123461510365868500E-2,  1.776455290340989370E-2, 7.553559379242526180E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (6 coeff)  et v (7 coeff) sont correctes, � ne pas inverser
/*
const   arm_iir_lattice_instance_f32  IIR_16k5_LPF =
{
    .numStages = 6,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.167552226048542,
		-0.509126096553661,
		 0.735096051952113,
		-0.787957750128232,
		 0.905593737375009,
  	    -0.629000975957534
    },

    .pvCoeffs = (float*) (const float[])
    {

    	0.007553559379243,
		0.040054927989750,
		0.110800079517240,
		0.172462758709134,
		0.138061784136484,
		0.034454527261521,
       -0.022230336661094
      }
};
*/

// Rx Audio LPF 16.5 KHz IIR filter ;  Elleptic stop band = -68 db  (92 µs max @ 15.56 KHz)  0.600 dB ripples // Nizar 10 Nov 2022
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 16.5 Khz  ( 0.34430) ,  Elleptic 4em ordre, stop band = -68 db  (92 µs max @ 15.56 KHz)  (  0.600dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,    -2.070790397417495930E0,   2.249825625864649400E0,  -1.272856241014585170E0 , 3.253093118996879700E-1 ]
// b = [   1.921822652442828130E-2, 5.713384999415112910E-2,  7.878414629509733480E-2,  5.713384999415112910E-2, 1.921822652442828130E-2]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
/*
const   arm_iir_lattice_instance_f32  IIR_16k5_LPF =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.325309311899688,
		-0.670125659205771,
		 0.827655676602523,
       	-0.710286401905560
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.019218226524428,
		0.096930768936331,
		0.215139068188932,
		0.196332351763607,
       	0.039313282574898
    }
};
*/
//*******************************************************************************************************

// Rx Audio LPF 11.5 KHz IIR filter ;  Elleptic stop band = -60 db  (25 µs max @ 8.5 KHz)  0.030 dB ripples // Nizar 24 Janvier 2024
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 11.5 Khz  ( 0.24000) ,  Elleptic 2em ordre, stop band = -60 db  (25 µs max @ 8.5 KHz)  (  0.030dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante:
// format long
// a = [  1.000000000000000000,    -1.031867559716559630E0,    3.799304348831901380E-1 ]
// b = [  8.761992972588981130E-2,  1.728230157148508140E-1,   8.761992972588981130E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (2 coeff)  et v (3 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_11k5_LPF =
{
    .numStages = 2,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.379930434883190,
    	-0.747767810341763
    },

    .pvCoeffs = (float*) (const float[])
    {
     	 0.087619929725890,
         0.263235178783641,
    	 0.251169244964664
    }
};
*/

//*******************************************************************************************************
// Rx Audio LPF 16.3 KHz IIR filter ;  Elleptic stop band = -70 db  (92 µs max @ 15.23 KHz)  0.600 dB ripples // Nizar 10 Nov 2022
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 16.3 Khz  ( 0.34010) ,  Elleptic 4em ordre, stop band = -70 db  (92 µs max @ 15.23 KHz)  (  0.600dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,      -2.106978657120660350E0,  2.295203436309523060E0,   -1.299618002003328780E0 ,  3.302385029615078520E-1 ]
// b = [  1.782498863831603500E-2, 5.411414951265963500E-2,   7.496700384509034980E-2,  5.411414951265963500E-2, 1.782498863831603500E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
// c'est un filtre passe bas 16.3Khz/96Khz  qui doit preceder la decimation ou suivre une interpolation  96Khz<-->48Khz  (pour un spectre 96Khz)


const   arm_iir_lattice_instance_f32  IIR_16k5_LPF =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	0.330238502961508,
	   -0.677723313664927,
		0.830678319386035,
       -0.721152508012816
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.017824988638316,
		 0.091671020137010,
		 0.206686990075470,
		 0.191977768591127,
      	 0.040820926356737
    }
};


//*******************************************************************************************************
//*******************************************************************************************************
// Rx Audio LPF 16.1 KHz IIR filter ;  Elleptic stop band = -70 db  (83 µs max @ 15.10 KHz)  0.340 dB ripples // Nizar 29 Juillet 2023
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 16.10 Khz  ( 0.3350) ,  Elleptic 4em ordre, stop band = -70 db  (83 µs max @ 15.10 KHz)  (  0.340dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,       -2.042823569816665690E0, 2.160816072652650630E0,    -1.176642327035005490E0,  2.842786526236825930E-1 ]
// b = [  1.815267725162189590E-2,   5.585406629063599300E-2,    7.761534134014659080E-2, 5.585406629063599300E-2, 1.815267725162189590E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
// c'est un filtre passe bas 16.10Khz/96Khz  qui doit preceder la decimation ou suivre une interpolation  96Khz<-->48Khz  (pour un spectre 96Khz)

const    arm_iir_lattice_instance_f32   IIR_16k1_LPF =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.284278652623683,
		-0.648303415719181,
		 0.823914562704801,
      	-0.726118509124891
   },

    .pvCoeffs = (float*) (const float[])
    {
	   0.018152677251622,
	   0.092936783235524,
	   0.211116056244215,
	   0.200443375668896,
	   0.044847544582806
    }
};

//*******************************************************************************************************

//*******************************************************************************************************
//*******************************************************************************************************
//*******************************************************************************************************
// Rx Audio LPF 16.8 KHz IIR filter ;  Elleptic stop band = -60 db  (71 µs max @ 15.94 KHz)  0.13 dB ripples // Nizar 07 Aout 2024
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 16.80 Khz  ( 0.3500) ,  Elleptic 4em ordre, stop band = -60 db  (71 µs max @ 15.94 KHz)  (  0.130dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,     -1.768305474254936000E0,    1.768459749598383150E0,       -8.939081699035217100E-1,   2.042627685799895950E-1 ]
// b     = [  2.709526281619219820E-2,   7.620533707215697560E-2,   1.039076742432164970E-1,       7.620533707215697560E-2,   2.709526281619219820E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
// c'est un filtre passe bas 16.10Khz/96Khz  qui doit preceder la decimation ou suivre une interpolation  96Khz<-->48Khz  (pour un spectre 96Khz)
/*
const    arm_iir_lattice_instance_f32   IIR_16k8_LPF =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	  0.204262768579990,
		 -0.555903306588035,
		  0.793977810089822,
      	 -0.676363012394942
   },

    .pvCoeffs = (float*) (const float[])
    {
	   0.027095262816192,
	   0.124118038636406,
	   0.261375753184398,
	   0.235306860156075,
	   0.042184646151586
    }
};

*/


//*******************************************************************************************************
//*******************************************************************************************************
//*******************************************************************************************************
// Rx Audio LPF 16.8 KHz IIR filter ;  Elleptic stop band = -65 db  (70.6 µs max @ 15.94 KHz)  0.13 dB ripples // Nizar 05 Aout 2025
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 16.80 Khz  ( 0.3500) ,  Elleptic 4em ordre, stop band = -65 db  (70.6 µs max @ 15.94 KHz)  (  0.130dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,      -1.777527280896147750E0,     1.774650281464459800E0,     -8.981403390191275090E-1,   2.046702258118165750E-1  ]
// b     = [  2.468066813431263550E-2,    7.510065553541646290E-2,    1.040902400215428350E-1,     7.510065553541645400E-2,   2.468066813431263550E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
// c'est un filtre passe bas 16.10Khz/96Khz  qui doit preceder la decimation ou suivre une interpolation  96Khz<-->48Khz  (pour un spectre 96Khz)

const    arm_iir_lattice_instance_f32   IIR_16k8_LPF =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.204670225811817,
		-0.557695227088553,
		 0.791726878702665,
      	-0.681935057149037
   },

    .pvCoeffs = (float*) (const float[])
    {
	    0.024680668134313,
	    0.118971216454901,
	    0.258185464358508,
	    0.237467547686044,
	    0.043504023671258
    }
};

//*******************************************************************************************************
//*******************************************************************************************************
// Tx Audio LPF 5.76 KHz IIR filter ;  Elleptic stop band = -74 db  (215 µs max @ 5.30 KHz)  0.500 dB ripples // Nizar 10 Nov 2022
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 5.76 Khz  ( 0.12000) ,  Elleptic 4em ordre, stop band = -74 db  (215 µs max @ 5.30 KHz)  (  0.500dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb 2018B avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -3.482720534412178990E0,   4.668438263973077260E0,  -2.846010909961487200E0 ,  6.647236356220423300E-1 ]
// b = [  1.048029592049657840E-3, 4.383126335635823880E-4,  1.457770770228048240E-3,  4.383126335635822990E-4, 1.048029592049657840E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
/*
const   arm_iir_lattice_instance_f32  IIR_5k7_LPF =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.664723635622042,
		-0.951305922150147,
		 0.976279527442339,
       	-0.972344488944250
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.001048029592050,
		 0.004088306814467,
		 0.008218275987438,
		 0.007748485973161,
     	 0.003751473071810
     }
};

*/
//*******************************************************************************************************

// Tx microphone  LPF 3.84 KHz IIR filter ;  Elleptic stop band = -74 db  (320 µs max @ 3.516 KHz)  0.500 dB ripples // Nizar 10 Nov 2022
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 3.84 Khz  ( 0.08010) ,  Elleptic 4em ordre, stop band = -74 db  (320 µs max @ 3.516 KHz)  (  0.500dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab 2018B  avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -3.677726197599226590E0,   5.128609570865743980E0,   -3.211439238706719210E0 ,   7.614879068091845230E-1 ]
// b = [  5.292459445495325100E-4, -4.953544105073998340E-4,  8.642583008992987810E-4,  -4.953544105073997450E-4, 5.292459445495325100E-4 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
/*
const   arm_iir_lattice_instance_f32  IIR_3k84_LPF =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.761487906809185,
		-0.978004861727236,
		 0.989402837279291,
    	-0.987907778504674
     },

    .pvCoeffs = (float*) (const float[])
    {

    	 0.000529245944550,
		 0.001451067264736,
		 0.002405924376040,
		 0.001707948667090,
     	 0.000852249767224
     }
};
*/

//*******************************************************************************************************

// Tx microphone  LPF 4.05 KHz IIR filter ;  Elleptic stop band = -76 db  (220 µs max @ 3.700 KHz)  0.050 dB ripples // Nizar 21 Avr 2023
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 4.05 Khz  ( 0.08440) ,  Elleptic 4em ordre, stop band = -76 db  (220 µs max @ 3.700 KHz)  (  0.050dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab 2018B  avec la fonction suivante :
// format long
// a = [  1.000000000000000000,      -3.527511934488246400E0,     4.724904392346911840E0,    -2.844133023441270500E0 ,    6.485500961317107740E-1 ]
// b = [  5.783558470492683230E-4,   -5.677951613073056510E-5,    7.663778872693868040E-4,   -5.677951613073059180E-5,    5.783558470492683230E-4 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
/*
const   arm_iir_lattice_instance_f32  IIR_Decim_4k_LPF_96k =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	  0.648550096131711,
		 -0.960271594764808,
		  0.985759597658982,
    	 -0.986086513594458
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.000578355847049,
		 0.001983377636717,
		 0.003794873764930,
		 0.003334447908325,
    	 0.001655065190076
     }
};

*/
//*******************************************************************************************************

// Tx microphone  LPF 4.13 KHz IIR filter ;  Elleptic 3em stop band = -76 db  (130 µs max @ 3.560 KHz)  0.050 dB ripples // Nizar 22 Avr 2023
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 4.13 Khz  ( 0.08610) ,  Elleptic 3em ordre, stop band = -76 db  (130 µs max @ 3.560 KHz)  (  0.050dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab 2018B  avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -2.572442435736314260E0,     2.248973437910956270E0,   -6.655217081432605930E-1 ]
// b = [  1.915037965419346570E-3,   3.589609050271554300E-3,    3.589609050271554300E-3,   1.915037965419346570E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser
/*
const   arm_iir_lattice_instance_f32  IIR_Decim_4k_LPF_96k =
{
    .numStages = 3,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.665521708143261,
		 0.963876514882571,
       	-0.983239870979340
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.001915037965419,
		0.008515933978562,
		0.015726681810100,
    	0.010444329132993
     }
};
*/
//*******************************************************************************************************

//***************************************************************************************************

// Tx microphone  LPF 4.38 KHz IIR filter ;  Elleptic 3em stop band = -76 db  (113 µs max @ 3.650 KHz)  0.010 dB ripples // Nizar 18 Aout  2024
// simulé par lowa Hills IIR filter designer  vers 6.5:
// LPF 4.38 Khz  ( 0.09130) ,  Elleptic 3em ordre, stop band = -76 db  (113 µs max @ 3.65 KHz)  (  0.010dB Ripples band ) fs = 96 Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab 2018B  avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,     -2.499777551765096200E0,   2.129131634192380940E0,       -6.144897261622094930E-1 ]
// b     = [  2.433659402009608290E-3,   4.998518730528011210E-3,    4.998518730528011210E-3,       2.433659402009608290E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

const   arm_iir_lattice_instance_f32  IIR_Decim_4k_LPF_96k =
{
    .numStages = 3,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.614489726162210,
		  0.952830569723792,
     	 -0.980255525274420
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.002433659402010,
		0.011082125872314,
		0.021031151362071,
      	0.013985632118408
    }
};

//*******************************************************************************************************

//***************************************************************************************************

