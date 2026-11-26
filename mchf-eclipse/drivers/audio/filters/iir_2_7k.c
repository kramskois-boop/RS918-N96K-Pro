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
//
// Rx Audio LPF 2.7K IIR filter ;  Elleptic stop band = -60 db  (22 ms max)  original
// lattice ARMA coefficients designed with MATLAb fdatools
// DD4WH 2016-03-05
// inspired by Clint KA7OEI
//
// LPF 2k7 ,  Elleptic 10em ordre, stop band = -60 db  (22 ms max)  ( 1 dB Ripples band )

/*

const arm_iir_lattice_instance_f32 IIR_2k7_LPF_S =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])  // a
    {
         0.276517739456119,
        -0.534458119364593,
         0.688627879533550,
        -0.657860665637030,
         0.883760854885895,
        -0.408163764378125,
         0.989378253422330,
        -0.218531341587163,
         0.999442039749756,
        -0.165077404674072
    },

    .pvCoeffs = (float*) (const float[])  // b
    {
        0.0155856864050234,
        0.0740622771621721,
        0.175501156733159,
        0.222640088831044,
        0.137215023898017,
        0.0116117418712191,
       -0.0432897242829734,
       -0.00519209144189112,
        0.00156095841010052,
        6.72317701914359e-05,
        9.08341518316962e-05
    }
};

*/


// Rx Audio LPF 2.7K IIR filter ;  Elleptic stop band = -70 db  (3.44 ms max) (  0.0db Ripples band ) fs = 12Khz  // Nizar 23 Fev 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k7 ( 0.4500) ,  Elleptic 10em ordre, stop band = -70 db  (3.44 ms max @2689 Hz)  (  0.0db Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.0000000000000000000, -2.06295231936133394 , 4.3833238092510225   , -5.26958312590995614, 5.71737738844338583, -4.36751007893832632, 2.83327946277509257,  -1.33126615722365038, 0.483636030035623588, -0.112738988913762017, 0.014051076870502941]
// b = [ 0.0104762969593732569, 0.0365470442759389158, 0.0911948222911868811, 0.159859159765020387, 0.222417500192440976, 0.246627450060678122, 0.222417500192440976, 0.159859159765020387, 0.0911948222911868811, 0.0365470442759389158, 0.0104762969593732569]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_2k7_LPF_S =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
     	 0.014051076870503,
		-0.083768826004856,
		 0.251179035475081,
		-0.410378544167893,
		 0.647204489795616,
		-0.485227875528627,
		 0.913525629585947,
		-0.289664841999105,
		 0.992553245531578,
	    -0.181989206831686
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.010476296959373,
		0.058159145386596,
		0.165184908494510,
		0.297577983249454,
		0.333905110149336,
		0.175131521293748,
	   -0.043082470139573,
	   -0.058548161365789,
	   -0.017666253213322,
		0.002138133739242,
		0.005024871122291
    }
};
*/


 // Rx Audio LPF IIR 2k71    definit par NIZAR 07/05/2023 pour essayer de reduire les distorsions de phase audio, a l'�coute ( 3.47 ms max )
 // simul� par lowa Hills IIR filter designer  ver 6.5 : 0 - 2.71Khz (0.4520)  , 10 poles , Elliptic, -70db stop band , fs = 12Khz  0.000dB riples
 // les coeffs treillis sont calcul�s par mathlab online via la fonction :
 // format long
 // a = [  1.000000000000000000,      -2.016773320411950450E0,     4.303576574757163260E0,    -5.121044533152152130E0,  5.569998441221049920E0,  -4.234469939784422540E0, 2.753842214963175830E0, -1.292110051988465620E0, 4.710357652813851100E-1, -1.099837566251035790E-1, 1.378732051683052570E-2 ]
 // b = [  1.066287540105399500E-2,   3.771386039625149960E-2,    9.441346832931722940E-2,    1.661621859212725430E-1, 2.315180950021557220E-1, 2.569177446774080220E-1, 2.315180950021556770E-1, 1.661621859212725210E-1, 9.441346832931721170E-2, 3.771386039625149960E-2, 1.066287540105399500E-2 ]
 // [k,v] = tf2latc(b,a)
 // NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
 // NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
 // NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser
 // theoriquement ce filtre semble etre le meilleur compromis pour ecoute Audio USB entre selectivit�-distorsion de phase

/*
const arm_iir_lattice_instance_f32 IIR_2k7_LPF_S =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	0.013787320516831,
	   -0.082193480622540,
		0.247780725148110,
	   -0.404850066795380,
	    0.645008655368868,
	   -0.478153921103662,
		0.913738928114646,
	   -0.282487893350020,
	    0.992571769061596,
	   -0.175651053300839
    },

    .pvCoeffs = (float*) (const float[])
    {
     	 0.010662875401054,
		 0.059218463023974,
		 0.167888075824111,
		 0.301234993160662,
		 0.335716934340209,
		 0.172962307878923,
		-0.047203624137805,
		-0.059435382331533,
		-0.016489242100674,
		 0.002328512162494,
    	 0.005018864550050
    }
};

*/



// Rx Audio LPF IIR 2k71    definit par NIZAR 22/04/2025 pour essayer de reduire les distorsions de phase audio, a l'�coute ( 1.066 ms max )
 // simule par lowa Hills IIR filter designer  ver 6.5 : 0 - 2.71Khz (0.4520)  , 6 poles , Elliptic, -40db stop band , fs = 12Khz  0.000dB riples
 // les coeffs treillis sont calcul�s par mathlab online via la fonction :
 // format long
 // a = [  1.000000000000000000,      -7.182864336320616160E-1,  1.457278151857787530E0,  -6.042584737893613500E-1, 4.830189360246245120E-1,  -8.958467963040341430E-2,  2.258429545994755210E-2 ]
 // b = [  6.958663471856590730E-2,   1.838740473453750250E-1,   3.290379489042493330E-1,  3.857545343541528560E-1, 3.290379489042493330E-1,   1.838740473453750470E-1,  6.958663471856590730E-2 ]
 // [k,v] = tf2latc(b,a)
 // NB : Les coeffs doivent etre copie avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
 // NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
 // NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser
 // theoriquement ce filtre semble etre le meilleur compromis pour ecoute Audio USB entre selectivit�-distorsion de phase

/*
const arm_iir_lattice_instance_f32 IIR_2k7_LPF_S =
{
    .numStages = 6,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.022584295459948,
		-0.073400124351275,
		 0.399890834533915,
		-0.252932113860660,
		 0.918699331288931,
    	-0.184376417309789
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.069586634718566,
		0.233857183025831,
		0.395219645706458,
		0.361011595931640,
		0.087427236213672,
	   -0.055643078334680,
       -0.072131686821377
   }
};

*/


// Rx Audio LPF 2.7K IIR filter ;  Elleptic stop band = -76 db  (11.9 ms max)  1.00dB ripples // Nizar 19 juillet 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k7  ( 0.4500) ,  Elleptic 10em ordre, stop band = -76 db  (11.9 ms max)  (  1.00dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.00000000000000000000,   -3.465672018828946490,   8.454368360177067390E0, -1.404654088382965020E1,   1.829520901773937110E1, -1.851121990999741840E1, 1.491563138939011160E1,   -9.349693232307355830E0,   4.439988946009403750E0, -1.459000653895814900E0,  2.774587698788879560E-1 ]
// b = [ 6.849667527605306280E-3,    1.690847674305642420E-2, 4.235428377386796230E-2,  6.672759182293241940E-2, 9.259766908657208970E-2,  9.965440642758983000E-2,  9.259766908657208970E-2,  6.672759182293241050E-2, 4.235428377386795340E-2,  1.690847674305642420E-2,  6.849667527605306280E-3]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_2k7_LPF =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.277458769878888,
		-0.538906388067598,
		 0.679030178046975,
		-0.702817929470082,
		 0.812700613327298,
		-0.549931345230059,
		 0.965793412160316,
		-0.285722334504026,
		 0.997833379343734,
      	-0.181545282715784
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.006849667527605,
		 0.040647177831759,
		 0.119236717251575,
		 0.196658028723970,
		 0.178001500876698,
		 0.061793852648391,
		-0.030972548736252,
		-0.014228493880011,
		-0.001235331797216,
		 0.000301826329245,
    	 0.000559233273866
     }
};   */

// Rx Audio LPF 2.7K IIR filter ;  Elleptic stop band = -52 db  (2.15 ms max)  0.00dB ripples // Nizar 30 juillet 2026
// simul� par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k7  ( 0.4500) ,  Elleptic 8em ordre, stop band = -52 db  (2.15 ms max)  ( 0.00 dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = 1, 2.62130257338695, -2.11259640613456, 1.94431553886434, -0.940541326239966, 0.440315048140617, -0.103016460786161, 0.0159240987940198
// b = 0.031477017523835, 0.0920110616020952, 0.197846075052305, 0.290573562004335, 0.333149524357847, 0.290573562004335, 0.197846075052305, 0.0920110616020952, 0.031477017523835
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (8 coeff)  et v (9 coeff) sont correctes, � ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_2k7_LPF =
{
    .numStages = 8,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.0159240987940198,
    	-0.0821968290570763,
    	 0.29318860286327,
    	-0.350794056380623,
    	 0.779520415505318,
    	-0.310015088673125,
    	 0.98108829894882,
    	-0.182331504858542
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.031477017523835,
    	 0.133206234002713,
    	 0.289493007821958,
    	 0.380258684751229,
    	 0.259895369975608,
    	 0.0164612741478156,
    	-0.122723770413409,
    	-0.020467877802543,
    	 0.00962175319633286
     }
};


// Rx Audio LPF 2.7K IIR filter ;  Elleptic stop band = -70 db  (10.1 ms max)  0.40dB ripples // Nizar 18 juillet 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k7  ( 0.4500) ,  Elleptic 10em ordre, stop band = -70 db  (10.1 ms max)  (  0.40dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.00000000000000000000,   -3.15959193485279943,   7.51090699436101961, -11.9136444921550111,   15.0871770371812985, -14.7287391344651253, 11.5272023154201619,   -6.9877585297413658,   3.21254187800816915, -1.01785340528774593,   0.184288991833392801 ]
// b = [ 0.00934127965525383175,    0.0220053301588088868, 5.562983317264720280E-2,  8.615110578478441640E-2, 1.199787191713442840E-1,  1.283171844163161880E-1,  1.199787191713442610E-1,  8.615110578478441640E-2, 5.562983317264720280E-2,  2.200533015880888680E-2,  9.341279655253831750E-3]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_2k7_LPF =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.184288991833393,
		-0.450888668999666,
		 0.634473955792387,
		-0.674079399294392,
		 0.815013653779851,
		-0.518589429399963,
		 0.969788402522280,
		-0.270122085319427,
		 0.998102388845639,
      	-0.178269652146017
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.009341279655254,
		0.051519962018753,
		0.143969416592426,
		0.230183248539924,
		0.199542680813530,
		0.060859607898900,
	   -0.041842525406622,
	   -0.015206885254694,
	   -0.000425709004716,
		0.000311481925537,
    	0.000548741614992
     }
};

*/



// Rx Audio LPF 2.7K IIR filter ;  Elleptic stop band = -80 db  (3.01 ms max)  0.00dB ripples // Nizar 07 Mai 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k7  ( 0.4500) ,  Elleptic 10em ordre, stop band = -80 db  (3.01 ms max)  (  0.00dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.00000000000000000000, -2.22354908959085762,  4.58959444495248636,   -5.70502851674837697,    6.06623937897229926, -4.71142140449206792, 3.00085871879283728,  -1.41936430326514063, 0.505223373264397324,  -0.117514625513794857,  0.0142331935549430133 ]
// b = [ 0.00645280940841098882,  0.0261930974237936498, 0.0680305195849324473, 0.124467928157334207, 0.176072841959229143,  0.196836776859324214, 0.176072841959229187, 0.124467928157334184, 0.0680305195849324473, 0.0261930974237936498,  0.00645280940841098882]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_2k7_LPF_S =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.014233193554943,
		-0.085883819613161,
		 0.250977232890623,
		-0.426332137063513,
		 0.624228248079949,
		-0.538341584180660,
		 0.883833248364750,
		-0.338114369594603,
		 0.988934632604968,
        -0.192355057483474
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.006452809408411,
		 0.040541235909169,
		 0.128510611918030,
		 0.258289976882883,
		 0.329743935752644,
		 0.217629076489486,
		 0.001431006430704,
		-0.063329204706015,
		-0.030862141108781,
		 0.002531052517024,
    	 0.007360399779446
    }
};   */

// Rx Audio LPF 2.7K IIR filter ;  Elleptic stop band = -42 db  (1.66 ms max)  0.00dB ripples // Nizar 01 Aout 2026
// simul� par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k7  ( 0.4500) ,  Elleptic 7 em ordre, stop band = -42 db  (1.66 ms max)  (  0.00dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = 1, -0.939925491158696, 1.97382951436829, -1.1335855394478, 1.02861720695356, -0.320975269038064, 0.129788045314781, -0.0125888978586478
// b = 0.056706173945626, 0.145549559482611, 0.285336959451044, 0.374987091687429, 0.374987091687429, 0.285336959451044, 0.145549559482611, 0.056706173945626
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (7 coeff)  et v (8 coeff) sont correctes, � ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_2k7_LPF_S =
{
    .numStages = 7,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.0125888978586478,
    	 0.117974115890939,
    	-0.188079876134925,
    	 0.643561965445683,
    	-0.275945353851682,
    	 0.967960759165552,
    	-0.178112548788557
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.056706173945626,
    	0.198849137880184,
    	0.360016689658896,
    	0.377381431853799,
    	0.164045683947851,
       -0.0963165669045001,
       -0.042734579140743,
       -0.0103087139549274
    }
};



/*
// Rx Audio LPF 2.75K IIR filter ;  Elleptic stop band = -80 db  (3.01 ms max)  0.00dB ripples // Nizar 26 Fev 2021
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k75 ( 0.4580) ,  Elleptic 10em ordre, stop band = -80 db  (3.01 ms max)  (  0.00dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.00000000000000000000, -2.0420377365549256   , 4.25978308361425562  , -5.10971345615380734, 5.45996931034561594 , -4.17694879499285143 , 2.67668442489661862, -1.2629229474253858  ,  0.454368887485009676 , -0.106634524332365577 ,  0.013182184444152365  ]
// b = [ 0.00698889277917462159,  0.0296568598908042613, 0.0783243509626228018, 0.145238557812113633, 0.206787540154632277,  0.231738028127620943, 0.206787540154632232, 0.145238557812113633,  0.0783243509626228107,  0.0296568598908042658,  0.00698889277917462159]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser

#define IIR_2k75_numStages 10
const   arm_iir_lattice_instance_f32  IIR_2k75_LPF
{
    .numStages = IIR_2k75_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	  0.013182184444152,
		 -0.079729860904050,
		  0.237064245640883,
		 -0.405462681498745,
		  0.613419066990369,
		 -0.512319661956419,
		  0.884840237075161,
		 -0.308479969475244,
		  0.989114138642434,
       	 -0.166697970589127
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.006988892779175,
		 0.043928442682615,
		 0.138210551998200,
		 0.273528366408483,
		 0.340681681195045,
		 0.212586993730763,
		-0.015300838210974,
		-0.068983324055214,
		-0.026300515330250,
		 0.003668724105880,
         0.007356398977336
     }
};
*/



// Rx Audio LPF 2.76K IIR filter ;  Elleptic stop band = -80 db  (5.27 ms max)  0.10dB ripples // Nizar 11 Avr 2023
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k760 ( 0.46000) ,  Elleptic 10em ordre, stop band = -80 db  (5.27 ms max)  (  0.10dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab
// format long
// a = [  1.000000000000000000,      -2.828107069648371660E0, 6.384496294728977620E0,    -9.471891955676515100E0 ,    1.138715700279796960E1, -1.050883200495084550E1, 7.802321592805551380E0, -4.474782320822296100E0, 1.939701685573236080E0,  -5.763203965166581180E-1,  9.486883157679331550E-2 ]
// b = [  6.102546437231097800E-3,  2.113443534937376360E-2,    5.300883056738761030E-2,  9.286157255030024870E-2,   1.294599333408009480E-1, 1.434770233776570870E-1,   1.294599333408009480E-1, 9.286157255030024870E-2, 5.300883056738761920E-2,  2.113443534937376360E-2,  6.102546437231097800E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser

/*
#define IIR_2k75_numStages 10
const   arm_iir_lattice_instance_f32  IIR_2k75_LPF =
{
    .numStages = IIR_2k75_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
     	 0.094868831576793,
		-0.310818580056965,
		 0.527195545008689,
		-0.627167233667968,
		 0.746967496010592,
		-0.564130226215104,
		 0.934983441087705,
		-0.295363572934806,
		 0.995041214989209,
    	-0.158766771561969
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.006102546437231,
		 0.038393090071365,
		 0.121494818031923,
		 0.230556133314790,
		 0.251096428147528,
		 0.120492658105003,
		-0.028360376176550,
		-0.031926068812069,
		-0.006324783647802,
		 0.001257235614852,
    	 0.001995008537803
    }
};
*/

// Rx Audio LPF 2.76K IIR filter ;  Elleptic stop band = -80 db  (3.60 ms max)  0.005 dB ripples // Nizar 11 oct 2025
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k760 ( 0.46000) ,  Elleptic 10em ordre, stop band = -80 db  (3.60 ms max)  (  0.005 dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab
// format long
// a = 1, -2.26475283944547, 4.81971400879389, -6.2182824176052, 6.88483637069053, -5.62728869500985, 3.78243688368289, -1.91347191158508, 0.733011020346694, -0.187126263616027, 0.0254233081209413
// b = 0.00673844271988858, 0.0271401996796998, 0.0704943303261937, 0.128816149271967, 0.182228152476281, 0.203664915425249, 0.182228152476281, 0.128816149271967, 0.0704943303261937, 0.0271401996796998, 0.00673844271988858
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser

#define IIR_2k75_numStages 10
const   arm_iir_lattice_instance_f32  IIR_2k75_LPF =
{
    .numStages = IIR_2k75_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.0254233081209413,
    	-0.129632541653767,
    	 0.323144657359845,
    	-0.486698015231158,
    	 0.66488046388562,
    	-0.536396334729999,
    	 0.903063485487986,
    	-0.304645881869313,
    	 0.99142651293529,
    	-0.160944557472854
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.00673844271988858,
    	 0.0424011069630081,
    	 0.133905250120314,
    	 0.264226672482371,
    	 0.319227476521124,
    	 0.18479701205916,
    	-0.0219823936368293,
    	-0.0562275470093359,
    	-0.0177483167735283,
    	 0.0028349278747389,
    	 0.00504227503614541
    }
};

// Rx Audio LPF 2.77K IIR filter ;  Elleptic stop band = -80 db  (3.01 ms max)  0.00dB ripples // Nizar 24 Avril 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k77 ( 0.4609) ,  Elleptic 10em ordre, stop band = -80 db  (3.01 ms max)  (  0.00dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.00000000000000000000, -1.97604696882006126   ,  4.1465535043987316 , -4.90308834775923597, 5.25465470816549018, -3.9948021984756128, 2.56732324935610379, -1.20979505468013904 ,  0.437119388423779309,  -0.102910427031825935, 0.0128210174120670439]
// b = [ 0.00719357741782404503,  0.031000148640749261, 0.082376203514689692,  0.1534778697674686,  0.219031536162009521,   0.245670199983815696,  0.219031536162009521, 0.1534778697674686, 0.082376203514689692,  0.0310001486407492655, 0.00719357741782404503]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser
/*
#define IIR_2k7_numStages 10
const   arm_iir_lattice_instance_f32  IIR_2k7_LPF =
{
    .numStages = IIR_2k7_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	  0.012821017412067,
		 -0.077588248276420,
		  0.232176323842964,
		 -0.397848948438073,
		  0.609588480392350,
		 -0.502552748435650,
		  0.885254081326869,
		 -0.297624041496490,
		  0.989179881066047,
   	     -0.157365839955361
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.007193577417824,
		 0.045214995492213,
		 0.141849626569576,
		 0.279089951509413,
		 0.344324497950504,
		 0.210161450064274,
		-0.021693574191595,
		-0.070833557053435,
		-0.024375571039064,
		 0.004083639765816,
       	 0.007308991933332
     }
};

*/




 // Rx Audio LPF IIR 2k7 definit par NIZAR pour essayer de reduire les distorsions de phase audio, � l'�coute ( 2.02 ms max)
 // simul� par lowa Hills IIR filter designer  vers 6.5:  0 - 2.7Khz, Papouli, 10 poles,  fs = 12Khz
 // Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction  :
 //
 //format long
 // a = [1.00000000000000000000, -3.10871732065926842    , 6.352826026095395480 ,-8.939790703570036360, 9.577830662990146850 , -7.899865717559023000 , 5.049428981698820710 , -2.450195405460327440, 0.8626762542265542070 , -0.1992332068446158730 ,  0.02303745029467041850 ]
 // b = [0.00026171584102765193,  0.00261715841027652063 , 0.0117772128462443448, 0.031405900923318244, 0.0549603266158069204,  0.0659523919389683133, 0.0549603266158069204,  0.031405900923318244, 0.0117772128462443448 ,  0.00261715841027652063,  0.00026171584102765193 ]
 // [k,v] = tf2latc(b,a)
 // NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
 // NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
 // NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser
/*
#define IIR_2k7_numStages 10
const arm_iir_lattice_instance_f32 IIR_2k7_LPF =
{
    .numStages = IIR_2k7_numStages,
    .pkCoeffs  = (float*) (const float[])  // a
    {
     	 0.023037450294670,
     	-0.127684051094902,
     	 0.325451583459221,
     	-0.517784188231734,
     	 0.635285461305358,
    	-0.683076123371484,
    	 0.752784248031444,
    	-0.619107840799261,
    	 0.932917180134125,
        -0.297466511603443
    },

    .pvCoeffs = (float*) (const float[])  // b
    {

    	0.000261715841028,
     	0.003430758978370,
    	0.020769745870059,
    	0.075650304099542,
    	0.174276318279840,
    	0.240629353078431,
    	0.166946965809181,
    	0.006671414504518,
       -0.056920126584286,
       -0.003194207125115,
        0.017364552059658,
    }
};
*/



 // Rx Audio LPF IIR 2k7    definit par NIZAR pour essayer de reduire les distorsions de phase audio, a l'�coute ( 1.42 ms max )
 // simul� par lowa Hills IIR filter designer  ver 6.5 : 0 - 2.7Khz , 10 poles , Inverse Chebyshev, -60db stop band , fs = 12Khz
 // les coeffs treillis sont calcul�s par mathlab online via la fonction :
 // format long
 // a = [ 1.000000000000000000 , 0.0550514081685438672, 1.7074722328677634  , 0.354449606209278612, 1.01916430007098313, 0.300102842310930651, 0.26625316028085404, 0.0760696412624159013, 0.0276461784309724212, 0.00494081751770871946, 0.000540128996405415052]
 // b = [ 0.0230255825391693847, 0.112257905097994359 , 0.311559110788379989, 0.599664329784376005, 0.86914550280725269, 0.980385454081512719, 0.86914550280725269, 0.599664329784375916 , 0.311559110788379989 , 0.112257905097994359  , 0.02302558253916938470 ]
 // [k,v] = tf2latc(b,a)
 // NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
 // NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
 // NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser
 // theoriquement ce filtre semble etre le meilleur compromis pour ecoute Audio USB entre selectivit�-distorsion de phase

/*
#define IIR_2k7_T_numStages 10
const arm_iir_lattice_instance_f32 IIR_2k7_LPF_T =
{
    .numStages = IIR_2k7_T_numStages,
    .pkCoeffs  = (float*) (const float[])
    {

    	0.000540128996405,
		0.004911084088620,
		0.026454219939023,
		0.066087788895568,
		0.216392324021418,
		0.173855050106791,
		0.656544828251348,
		0.060325811197986,
		0.953738668575539,
	   -0.107504933188777
    },

    .pvCoeffs = (float*) (const float[])
    {
    	  0.023025582539169,
		  0.110990314355312,
		  0.266133689273822,
		  0.387375890708375,
		  0.331421915349204,
		  0.094460927736170,
		 -0.135107713716374,
		 -0.149169590049521,
		 -0.034824909833453,
		  0.016209685053528,
    	  0.034346751400878
     }
};

*/

// Rx Audio LPF IIR 2k7    definit par NIZAR pour essayer de reduire les distorsions de phase audio, a l'�coute ( 6.4 ms max )
 // simul� par lowa Hills IIR filter designer  ver 6.5 : 0 - 2.7Khz (0.450)  , 10 poles , Elliptic, -70db stop band , fs = 12Khz  0.06dB riples
 // les coeffs treillis sont calcul�s par mathlab online via la fonction :
 // format long
 // a = [ 1.00000000000000      , -2.78331402268307171 , 6.30598020665396408  , -9.22718269184116835, 11.0367543852693428   , -10.0182242818494927, 7.34428977724031018, -4.1208611164655311, 1.74803494551938887, -0.503378696048444851, 0.0795881856301711643]
 // b = [ 0.00949910777916111826, 0.0261673918567789654, 0.0645381715665078559,  0.105555456694610261, 0.1456913894896941560, 0.1587836566519647, 0.145691389489694156, 0.105555456694610283,  0.0645381715665078559, 0.0261673918567789654, 0.00949910777916111826]
 // [k,v] = tf2latc(b,a)
 // NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
 // NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
 // NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser
 // theoriquement ce filtre semble etre le meilleur compromis pour ecoute Audio USB entre selectivit�-distorsion de phase

/*
#define IIR_2k7_T_numStages 10
const arm_iir_lattice_instance_f32 IIR_2k7_LPF_T =
{
    .numStages = IIR_2k7_T_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.079588185630171,
		-0.283656542708578,
		 0.512208570424576,
		-0.612995502593183,
		 0.770138392448469,
		-0.529498832100956,
		 0.953887274393428,
		-0.284615458344081,
		 0.996762032516743,
    	-0.180952737645039
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.009499107779161,
		0.052606391741496,
		0.149869467125297,
		0.258696956374143,
		0.253431778549934,
		0.099656750629059,
	   -0.043922821171319,
	   -0.026529992550069,
	   -0.003808803993887,
	    0.000681504719932,
    	0.001337839594763,
     }
};

*/



// Rx Audio LPF IIR 2k7    definit par NIZAR pour essayer de reduire les distorsions de phase audio, a l'�coute ( 3.1 ms max )
 // simul� par lowa Hills IIR filter designer  ver 6.5 : 0 - 2.7Khz , 10 poles ,  Chebyshev, 0.2db ripples , fs = 12Khz
 // les coeffs treillis sont calcul�s par Mathlab online via la fonction :
 // format long
 // a = [ 1.000000000000000000, -3.97940851303799992  , 9.44208382612047004   , -15.5303584348744339  , 19.2311003002750458  , -18.4078810955718986  , 13.7084485329831751  , -7.826646617147818400 , 3.289684888425917110  , -0.92822060730602427  ,  0.13691694506219485    ]
 // b = [ 0.000132538305594362,  0.0013253830559436226, 0.00596422375174630126,  0.0159045966713234677, 0.0278330441748160684,  0.033399653009779291,  0.0278330441748160728,  0.0159045966713234699, 0.00596422375174630304,  0.0013253830559436226,  0.000132538305594362305]
 // [k,v] = tf2latc(b,a)
 // NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
 // NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
 // NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser
/*

#define IIR_2k7_numStages 10
const arm_iir_lattice_instance_f32 IIR_2k7_LPF =
{
    .numStages = IIR_2k7_numStages,
    .pkCoeffs  = (float*) (const float[])  // a
    {
      		0.136916945062195,
    	   -0.390696239872588,
    		0.591501698263190,
    	   -0.684643344659028,
    		0.722849817024267,
    	   -0.732283328640686,
     	 	0.775330700424932,
    	   -0.644354159084327,
    		0.935761588221647,
       	   -0.307534783822037
      },

    .pvCoeffs = (float*) (const float[])  // b
    {

    		0.000132538305594,
    		0.001852807117529,
    		0.011986750284830,
    		0.045273719424192,
    		0.104008586357509,
    		0.142100084559449,
    		0.099419434488887,
    		0.007480723018222,
    	   -0.030364106913355,
    	   -0.002018056194240,
       	    0.009149616831521
    }
};

*/






 // Rx Audio LPF IIR 2k7    definit par NIZAR pour essayer de reduire les distorsions de phase audio, a l'�coute ( 1.03 ms max )
 // simul� par lowa Hills IIR filter designer vers 6.5  : 0 - 2.7Khz , 10 poles , Butterworth,  fs = 12Khz
 // Les coeff treillis sont Calcul�s  avec mathlab par la fonction :
 // format long
 // a = [ 1.00000000000000000000, -0.99600985716836874   , 1.75955882059809698  , -1.11209789308878682, 0.874743027209397006, -0.347447044820450746, 0.144163370056258766, -0.0330711613940974969, 0.00669189040512852884, -0.000679889655126410197, 0.0000391470990890749926 ]
 // b = [ 0.00126551797777455111,  0.01265517977774551770, 0.0569483089998548131,  0.15186215733294619, 0.265758775332655794,  0.318910530399186998, 0.265758775332655839,  0.15186215733294619  , 0.0569483089998548131 ,  0.0126551797777455177  , 0.00126551797777455111   ]
 // [k,v] = tf2latc(b,a)
 // NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
 // NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
 // NB : les correspondances entre k (10 coeff)  et v (11 coeff) sont correctes, � ne pas inverser

/*
#define IIR_2k7_numStages 10
const arm_iir_lattice_instance_f32 IIR_2k7_LPF =
{
    .numStages = IIR_2k7_numStages,
    .pkCoeffs  = (float*) (const float[])  // a
    {
    		 0.000039147099089,
    		-0.000640898759536,
    		 0.005984669784120,
    		-0.025940102327595,
    		 0.107130565778596,
    		-0.190605810497498,
    		 0.493617356072020,
    		-0.335682960633936,
    		 0.899260915365407,
		    -0.216400087999179
      },

    .pvCoeffs = (float*) (const float[])  // b
    {
    	0.001265517977775,
    	0.013915648158033,
    	0.068581678064589,
    	0.197091901959980,
    	0.355729359738162,
    	0.390071662312935,
    	0.185689120217429,
       -0.102846977657765,
       -0.162183512430388,
    	0.003255580060449,
        0.062583421272260
     }
};
*/


//**************************************************************************************************************


// Interpolation Noise Reduction LPF  filter  6Khz -----> 12Khz
// Rx Audio LPF 2.86K IIR 8em ordre filter ;  Elleptic stop band = -62 db  (1.20 ms 2.7Khz)  0.020dB ripples // Nizar 14 Avr 2023
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k86 ( 0.47720) ,  Elleptic 8em ordre, stop band = -62 db  (1.20 ms 2.7khz)  (  0.020dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,      -1.430115059130699470E0,  2.916207766026353450E0,    -2.719901718570384650E0 ,     2.594755197665434390E0,  -1.531832295481172280E0,  7.685186409790936150E-1, -2.368437457723385100E-1, 4.264649115633321940E-2 ]
// b = [   2.203050406813184200E-2,   7.801755458242575440E-2,    1.755646723554444400E-1,  2.702077017202695770E-1,   3.117944114200758680E-1, 2.702077017202695330E-1,   1.755646723554444400E-1,  7.801755458242575440E-2,  2.203050406813184200E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (8 coeff)  et v (9 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_Noise_Reduction_Interpolation_2k86_LPF =
{
    .numStages = 8,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.042646491156333,
		-0.176174769692877,
		 0.407342651256059,
		-0.465646779492126,
		 0.802957312637759,
		-0.313917315234198,
		 0.983037917311676,
    	-0.114274777910301
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.022030504068132,
		 0.109523710210501,
		 0.267127775150521,
		 0.374595241072303,
		 0.253389298856705,
		 0.001480713609638,
		-0.113635797810266,
		-0.013689039688145,
       	 0.013150843479023
    }
};
*/

// Interpolation Noise Reduction LPF  filter  6Khz -----> 12Khz
// Rx Audio LPF 2.86K IIR 8em ordre filter ;  Elleptic stop band = -68 db  (1.45 ms 2.7Khz)  0.020dB ripples // Nizar 14 Avr 2023
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k86 ( 0.47620) ,  Elleptic 9em ordre, stop band = -68 db  (1.45 ms 2.7khz)  (  0.020dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,      -1.736888500598287250E0,      3.714031792302839640E0,   -4.106048523592170870E0 ,    4.357503460804047710E0,     -3.132604521520703230E0,      1.921494173288490700E0,     -8.233531792571204380E-1,    2.536200140923304150E-1,    -4.155149145885353330E-2 ]
// b     = [  1.476224247394628540E-2,    5.432478832177119800E-2,     1.311488413137018580E-1,    2.199521913532966890E-1,   2.829135485675705830E-1,     2.829135485675705830E-1,     2.199521913532967330E-1,     1.311488413137018580E-1,    5.432478832177119800E-2,     1.476224247394628540E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (9 coeff)  et v (10 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_Noise_Reduction_Interpolation_2k86_LPF =
{
    .numStages = 9,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.041551491458854,
		  0.181763525928529,
		 -0.368014984820078,
		  0.588879799911882,
		 -0.499141571366605,
		  0.886250374800619,
		 -0.262590093742742,
		  0.991152357370196,
         -0.108701631761740
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.014762242473946,
		0.079965157517812,
		0.214608025482824,
		0.342574227081431,
		0.289009537137958,
		0.054707815487184,
	   -0.062361507313993,
	   -0.050847675947987,
		0.000314801082041,
      	0.007913165375153
    }
};
*/

// Interpolation Noise Reduction LPF  filter  6Khz -----> 12Khz
// Rx Audio LPF 2.86K IIR 8em ordre filter ;  Elleptic stop band = -66 db  (1.40 ms 2.7Khz)  0.010dB ripples // Nizar 12 oct 2025
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k86 ( 0.47620) ,  Elleptic 9em ordre, stop band = -66 db  (1.40 ms 2.7khz)  (  0.010dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = 1, -1.58010311040814, 3.4443226083572, -3.57497971006253, 3.80058210834228, -2.60124170421303, 1.57902695631754, -0.647161278103495, 0.194819498951047, -0.0302364885073882
// b = 0.0165797882600397, 0.0611899442988672, 0.147767959569809, 0.247978101296602, 0.318998646911425, 0.318998646911425, 0.247978101296602, 0.147767959569809, 0.0611899442988672, 0.0165797882600397
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (9 coeff)  et v (10 coeff) sont correctes, � ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_Noise_Reduction_Interpolation_2k86_LPF =
{
    .numStages = 9,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.0302364885073882,
    	 0.147177285545045,
    	-0.318512991498066,
    	 0.558971152368998,
    	-0.467322584536771,
    	 0.882821164380836,
    	-0.248843467453069,
    	 0.990801987481132,
    	-0.105844047971668
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.0165797882600397,
    	 0.0873877192984642,
    	 0.228354540885527,
    	 0.356797874424178,
    	 0.296916114459901,
    	 0.049831004563859,
    	-0.068757561816972,
    	-0.0540290138507007,
    	 0.000407942827284978,
    	 0.0087428874884545
    }
};


// Interpolation Noise Reduction LPF  filter  6Khz -----> 12Khz
// Rx Audio LPF 2.84K IIR 7em ordre filter ;  Elleptic stop band = -70 db  (2.70 ms 2.818Khz)  0.060dB ripples // Nizar 31 aout  2025
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k84 ( 0.47260) ,  Elleptic 8em ordre, stop band = -70 db  (2.70 ms 2.818khz)  (  0.060dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,       -1.831142091948632980E0,    3.503285225524182420E0,   -3.775526509614212770E0 ,    3.532550367596680730E0,     -2.297652464087403420E0,      1.160201679992595380E0,      -3.850434961144276080E-1,    7.162475601517233060E-2 ]
// b     = [  1.440396116741918590E-2,    5.344674651479856210E-2,    1.219030383126880280E-1,   1.897158723961885230E-1,    2.193582305817663780E-1,     1.897158723961885230E-1,      1.219030383126880500E-1,      5.344674651479857100E-2,    1.440396116741918590E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (7 coeff)  et v (8 coeff) sont correctes, � ne pas inverser
// NB : le 7em ordre semble un bon compromis entre 8 et 5 pour doser les images audio aux alentours de 6Khz +/-2.7K
/*
const   arm_iir_lattice_instance_f32   IIR_Noise_Reduction_Interpolation_2k86_LPF =
{
    .numStages = 8,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.071624756015172,
    	-0.255197581108389,
    	 0.482770917601243,
    	-0.554775419970776,
    	 0.797868815552116,
    	-0.391032291620836,
    	 0.979868823722277,
        -0.143464751242082
    },

    .pvCoeffs = (float*) (const float[])
    {
         0.014403961167419,
    	 0.079822446099253,
         0.216149063154553,
     	 0.334901796176758,
    	 0.259754542702780,
    	 0.030289816392601,
    	-0.095770317907793,
    	-0.015213648310775,
     	 0.011441552672643
    }
};



*/
// Interpolation Noise Reduction LPF  filter  6Khz -----> 12Khz
// Rx Audio LPF 2.84K IIR 7em ordre filter ;  Elleptic stop band = -56 db  (2.14 ms 2.81Khz)  0.060dB ripples // Nizar 10 Sept 2023
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k84 ( 0.47340) ,  Elleptic 7em ordre, stop band = -56 db  (2.14 ms 2.81khz)  (  0.060dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,      -1.430115059130699470E0,  2.916207766026353450E0,    -2.719901718570384650E0 ,     2.594755197665434390E0,  -1.531832295481172280E0,  7.685186409790936150E-1, -2.368437457723385100E-1, 4.264649115633321940E-2 ]
// b = [  2.203050406813184200E-2,   7.801755458242575440E-2,    1.755646723554444400E-1,  2.702077017202695770E-1,   3.117944114200758680E-1, 2.702077017202695330E-1,   1.755646723554444400E-1,  7.801755458242575440E-2,  2.203050406813184200E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (7 coeff)  et v (8 coeff) sont correctes, � ne pas inverser
// NB : le 7em ordre semble un bon compromis entre 8 et 5 pour doser les images audio aux alentours de 6Khz +/-2.7K
/*
const   arm_iir_lattice_instance_f32   IIR_Noise_Reduction_Interpolation_2k86_LPF =
{
    .numStages = 7,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.070829138128956,
		 0.267120305786430,
		-0.432750697675894,
		 0.718681036806019,
		-0.397528089430628,
		 0.971265991052260,
        -0.142565927434293
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.030464450616883,
		0.138031645815767,
		0.305080758584066,
		0.367980294401530,
		0.168702997968893,
	   -0.075063257783180,
	   -0.035486689738488,
   	   -0.001774005328748
    }
};
*/

// Interpolation Noise Reduction LPF  filter  6Khz -----> 12Khz
// Rx Audio LPF 2.84K IIR 7em ordre filter ;  Elleptic stop band = -54 db  (1.84 ms 2.81Khz)  0.020dB ripples // Nizar 04 juillet 2025
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k84 ( 0.47340) ,  Elleptic 7em ordre, stop band = -54 db  (1.84 ms 2.81khz)  (  0.020dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,      -1.190229786138633060E0,   2.297623364791278270E0,  -1.767150083554783980E0,  1.481482986932796560E0,  -6.739829654445033570E-1,  2.517956780665230100E-1,  -4.323018981976439080E-2 ]
// b = [  3.460276160586781110E-2,    1.104103643289141790E-1,  2.249076306917089600E-1,  3.082337457899655850E-1, 3.082337457899655850E-1,  2.249076306917089600E-1,  1.104103643289141790E-1,   3.460276160586781110E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (7 coeff)  et v (8 coeff) sont correctes, � ne pas inverser
// NB : le 7em ordre semble un bon compromis entre 8 et 5 pour doser les images audio aux alentours de 6Khz +/-2.7K
/*
const   arm_iir_lattice_instance_f32   IIR_Noise_Reduction_Interpolation_2k86_LPF =
{
    .numStages = 7,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.043230189819764,
		  0.200716928175711,
		 -0.352787436614095,
		  0.684869921951894,
		 -0.362080980687120,
		  0.967890487258289,
     	 -0.137556298189824
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.034602761605868,
		 0.151595601874872,
		 0.324521718216309,
		 0.385305501151590,
		 0.177548024799550,
		-0.085699043989265,
		-0.041807272028064,
       	-0.002242922575860
    }
};
*/


// Interpolation Noise Reduction LPF  filter  6Khz -----> 12Khz
// Rx Audio LPF 2.84K IIR 6em ordre filter ;  Elleptic stop band = -48 db  (1.26 ms 2.83Khz)  0.020dB ripples // Nizar 04 juillet 2025
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k85 ( 0.47570) ,  Elleptic 6em ordre, stop band = -48 db  (1.26 ms 2.83khz)  (  0.020dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,      -8.779731346158584420E-1,     1.676643099412927110E0,      -9.627178215319673880E-1,   7.029673549374962250E-1,  -2.147140992734636540E-1,  4.825441182977960430E-2  ]
// b     = [  5.256275230296574290E-2,    1.603885891790382210E-1,     2.960160961490604590E-1,      3.545249354967847650E-1,   2.960160961490604590E-1,   1.603885891790382210E-1,  5.256275230296574290E-2  ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (6 coeff)  et v (7 coeff) sont correctes, � ne pas inverser
// NB : le 7em ordre semble un bon compromis entre 8 et 5 pour doser les images audio aux alentours de 6Khz +/-2.7K
/*
const    arm_iir_lattice_instance_f32    IIR_Noise_Reduction_Interpolation_2k86_LPF_2  =
{
    .numStages = 6,
    .pkCoeffs  = (float*) (const float[])
    {
    	  0.048254411829780,
		 -0.172750269033764,
		  0.487842224834731,
		 -0.354753924817744,
		  0.930717826919315,
       	 -0.146123139549976
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.052562752302966,
		 0.206537273582510,
		 0.387499610723002,
		 0.369380470420659,
		 0.080624511129211,
		-0.061901307505790,
        -0.056377646591243
    }
};
*/

// Intermodulation Noise Reduction LPF  filter  6Khz -----> 12Khz
// Rx Audio LPF 2.86K IIR 8em ordre filter ;  Elleptic stop band = -42 db  (1.09 ms 2.82Khz) 0.160dB ripples // Nizar 11 Sept 2023
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 2k86 ( 0.47610) ,  Elleptic 5em ordre, stop band = -42 db  (1.09 ms 2.82khz)  (  0.160dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -9.106686192993683090E-1,   1.500886440385840180E0,   -8.351181826171156790E-1,  4.726788523287865470E-1,  -1.081109798420420760E-1 ]
// b = [  7.295428270944819720E-2,    1.880835076096937230E-1,  2.987959651589084050E-1,  2.987959651589084050E-1, 1.880835076096937230E-1, 7.295428270944819720E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (5 coeff)  et v (6 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_Noise_Reduction_Interpolation_2k86_LPF =
{
    .numStages  = 5,
    .pkCoeffs   = (float*) (const float[])
    {
       -0.108110979842042,
		0.378651245266958,
	   -0.410318089185249,
		0.892404106019391,
       -0.183996250092199
     },

    .pvCoeffs = (float*) (const float[])
    {
     	 0.072954282709448,
		 0.254520683516683,
		 0.410664721312461,
		 0.289812778217230,
		 0.002641154416481,
   	    -0.105174119067624
    }
};
*/

//**********************************************************************************************************
// Rx Audio LPF 52.2 Hz IIR 3em ordre filter ;  Elleptic stop band = -30 db  (9.16 ms 46.0 Hz) 0.000dB ripples // Nizar 15 Oct 2023
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 52.2 Hz ( 0.00870) ,  Elleptic 3em ordre, stop band = -30 db  (9.16 ms 46.0 Hz)  (  0.000dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -9.106686192993683090E-1,   1.500886440385840180E0,   -8.351181826171156790E-1,  4.726788523287865470E-1,  -1.081109798420420760E-1 ]
// b = [  7.295428270944819720E-2,    1.880835076096937230E-1,  2.987959651589084050E-1,  2.987959651589084050E-1, 1.880835076096937230E-1, 7.295428270944819720E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_LPF_52Hz_Compressor  =
{
    .numStages  =3,
    .pkCoeffs   = (float*) (const float[])
    {
      	  -0.949227993030304,
       	   0.999492820130965,
          -0.999790356287637
     },

    .pvCoeffs = (float*) (const float[])
    {
     	 0.002546735796508,
     	 0.004971224768270,
    	 0.000023679378567,
    	 0.000019139656596
    }
};
*/

//**********************************************************************************************************
// Rx Audio LPF 60.4 Hz IIR 3em ordre filter ;  Elleptic stop band = -36 db  (7.80 ms 43.0 Hz) 0.000dB ripples // Nizar 15 Oct 2023
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 60.4 Hz ( 0.010007) ,  Elleptic 3em ordre, stop band = -36 db  (7.80 ms 43.0 Hz)  (  0.000dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// format long
// a = [  1.000000000000000000,      -2.940273708380949810E0,   2.882424389292296500E0,   -9.421198867477651010E-1 ]
// b = [  1.831503335220031390E-3,   -1.816106253429375040E-3, -1.816106253429375040E-3,  1.831503335220031390E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_LPF_52Hz_Compressor  =
{
    .numStages  = 3,
    .pkCoeffs   = (float*) (const float[])
    {
    	 -0.942119886747765,
		  0.999323345175205,
      	 -0.999733893181286
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.001831503335220,
		0.003569014849930,
		0.000038439737074,
    	0.000028828699162
    }
};
*/
//**********************************************************************************************************
// Rx Audio LPF 60.0 Hz IIR 3em ordre filter ;  Elleptic stop band = -30 db  (7.94 ms 53.0 Hz) 0.000dB ripples // Nizar 06 Dec 2023
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 60.0 Hz ( 0.010000) ,  Elleptic 3em ordre, stop band = -30 db  (7.94 ms 53.0 Hz)  (  0.000dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// format long
// a = [  1.000000000000000000,      -2.940010505577068220E0,   2.881908077781720310E0,    -9.418653762875957010E-1 ]
// b = [  2.916926696034907620E-3,   -2.900828737506841560E-3, -2.900828737506842000E-3,  2.916926696034907620E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_LPF_52Hz_Compressor  =
{
    .numStages  = 3,
    .pkCoeffs   = (float*) (const float[])
    {
       -0.941865376287596,
		0.999329997758356,
       -0.999722998926519,
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.002916926696035,
		 0.005674966392834,
		 0.000035844310017,
      	 0.000028949184674
     }
};  */

//**********************************************************************************************************
//**********************************************************************************************************
// Rx Audio LPF 75.0 Hz IIR 3em ordre filter ;  Elleptic stop band = -30 db  (7.94 ms 68.0 Hz) 0.090dB ripples // Nizar 08 Mars 2024
// simulation par lowa Hills IIR filter designer  vers 6.5:
// LPF 60.0 Hz ( 0.012500) ,  Elleptic 3em ordre, stop band = -30 db  (7.94 ms 68.0 Hz)  (  0.090dB Ripples band ) fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,     -2.942900968553780140E0,    2.888026455492283870E0,   -9.450815346393232730E-1  ]
// b     = [  3.205102833671475080E-3,  -3.183126684081090120E-3,  -3.183126684081090120E-3,   3.205102833671475080E-3  ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_LPF_52Hz_Compressor  =
{
    .numStages  = 3,
    .pkCoeffs   = (float*) (const float[])
    {
    	 -0.945081534639323,
		  0.999290391216468,
      	 -0.999599698399298
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.003205102833671,
		 0.006249173549445,
		 0.000049362836927,
       	 0.000038790334294
    }
};

//**********************************************************************************************************

//**********************************************************************************************************
// Rx Audio HPF 48 Hz IIR filter  (0.008);  PAPOULI  // Nizar 22 Mars 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// HPF 48 Hz ( 0.0080) ,  PAPOULI 3em ordre,  fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.000000000000000000, -2.94644738613308466,  2.89426904730124868, -0.947800757524850468 ]
// b = [ 0.973564648869897908, -2.92069394660969373,  2.92069394660969373, -0.973564648869897908 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

#define IIR_HPF_50HZ_numStages 3
const   arm_iir_lattice_instance_f32  IIR_50HZ_HPF =
{
    .numStages = IIR_HPF_50HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.947800757524850,
		 0.999510775093601,
		-0.999799721635099
    },

    .pvCoeffs = (float*) (const float[])
    {
    	-0.973564648869898,
		 0.052136931715409,
		 0.001291560216015,
       	-0.000000786313830
     }
};


// Rx Audio HPF 65 Hz IIR filter  (0.01084) 3em ordre;  PAPOULI  3 em ordre// Nizar 11 Mars 2022
// simul� par lowa Hills IIR filter designer  vers 6.5:
// HPF 65 Hz ( 0.01084) ,  PAPOULI 3em ordre,  fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -2.927461918840513990E0,   2.857443639824655430E0,  -9.299302017835707090E-1 ]
// b = [  9.643544700560923390E-1,  -2.893063410168277280E0,  2.893063410168277280E0,   -9.643544700560923390E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

#define IIR_HPF_65HZ_numStages 3
const   arm_iir_lattice_instance_f32  IIR_65HZ_HPF =
{
    .numStages = IIR_HPF_65HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.929930201783571,
		 0.999102025815312,
      	-0.999632207143919
    },

    .pvCoeffs = (float*) (const float[])
    {
    	-0.964354470056092,
		 0.069952422815442,
		 0.002315734091352,
       	-0.000002601838759
     }
};



// Rx Audio HPF 70 Hz IIR filter  (0.0117);  PAPOULI  // Nizar 23 Mars 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// HPF 70 Hz ( 0.0117) ,  PAPOULI 3em ordre,  fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.000000000000000000, -2.92171533930535698,  2.84636494295643727, -0.924585007033105555 ]
// b = [ 0.961583161161862421, -2.88474948348558735,  2.88474948348558735, -0.961583161161862421 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser
/*
#define IIR_HPF_70HZ_numStages 3
const   arm_iir_lattice_instance_f32  IIR_70HZ_HPF =
{
    .numStages = IIR_HPF_70HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.924585007033106,
		 0.998953998409964,
       	-0.999571501519796
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 -0.961583161161862,
		  0.075277211501239,
		  0.002678320500889,
    	 -0.000003511237584
      }
};
*/


// Rx Audio HPF 75 Hz IIR filter  (0.0125) 3em ordre;  PAPOULI  3 em ordre// Nizar 23 Mars 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// HPF 75 Hz ( 0.0125) ,  PAPOULI 3em ordre,  fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.000000000000000000, -2.9163707272893804,  2.83608940129911824, -0.919640106965414006 ]
// b = [ 0.959012529444239092, -2.87703758833271728, 2.87703758833271728, -0.959012529444239092 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

#define IIR_HPF_75HZ_numStages 3
const   arm_iir_lattice_instance_f32  IIR_75HZ_HPF =
{
    .numStages = IIR_HPF_75HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.919640106965414,
		 0.998806187727408,
     	-0.999510863152492
    },

    .pvCoeffs = (float*) (const float[])
    {
    	-0.959012529444239,
		 0.080201520357793,
		 0.003036565088471,
     	-0.000004550720946
     }
};


// Rx Audio HPF 75 Hz IIR filter  (0.0125);  PAPOULI  // Nizar 23 Mars 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// HPF 75 Hz ( 0.0125) ,  PAPOULI 4em ordre,  fs = 12Khz  (9.5 ms max )
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.000000000000000000, -3.88815669309070522, 5.67068300710456352,  -3.6766943817136295, 0.894171987277317903 ]
// b = [ 0.945606629324138481, -3.7824265172965541,  5.67363977594482982,  -3.7824265172965541, 0.945606629324138481 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (4 coeff)  et v (5 coeff) sont correctes, � ne pas inverser
/*
#define IIR_HPF_75HZ_numStages 4
const   arm_iir_lattice_instance_f32  IIR_75HZ_HPF =
{
    .numStages = IIR_HPF_75HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.894171987277318,
		-0.997790679493797,
		 0.999552582690642,
    	-0.999531587690949
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 0.945606629324139,
		-0.105759772458963,
		-0.005447739354716,
		 0.000010757787332,
       	 0.000001609863913
     }
};
*/


// Rx Audio HPF 75 Hz IIR filter  (0.0125) 2em ordre;  PAPOULI  // Nizar 23 Mars 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// HPF 75 Hz ( 0.0125) ,  PAPOULI 2em ordre,  fs = 12Khz  (3.6 ms max )
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.000000000000000000, -1.94444591348792217, 0.94594788539333301  ]
// b = [ 0.972598449720313774, -1.9451968994406276,  0.972598449720313774 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (2 coeff)  et v (3 coeff) sont correctes, � ne pas inverser
/*
#define IIR_HPF_75HZ_numStages 2
const   arm_iir_lattice_instance_f32  IIR_75HZ_HPF =
{
    .numStages = IIR_HPF_75HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.945947885393333,
    	-0.999228154095653
    },

    .pvCoeffs = (float*) (const float[])
    {
    	  0.972598449720314,
		 -0.054031818417275,
     	 -0.001419111308976
    }
};
*/

// Rx Audio HPF 100 Hz IIR filter  (0.0167) 3em ordre;  PAPOULI  // Nizar 24 Mars 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// HPF 100 Hz ( 0.0167) ,  PAPOULI 3em ordre,  fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.000000000000000000, -2.88832768456559474,  2.78261865014997412, -0.89410617113617441 ]
// b = [ 0.945631563231467886, -2.83689468969440384,  2.83689468969440384, -0.945631563231467886]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

#define IIR_HPF_100HZ_numStages 3
const   arm_iir_lattice_instance_f32  IIR_100HZ_HPF =
{
    .numStages = IIR_HPF_100HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.894106171136174,
		 0.997870555055543,
       	-0.999126524110493
    },

    .pvCoeffs = (float*) (const float[])
    {
    	-0.945631563231468,
		 0.105600866213914,
		 0.005229912319594,
       	-0.000014103940882
    }
};


// Rx Audio HPF 150 Hz IIR filter  (0.025) 3 em ordre;  PAPOULI  (3.2 ms max) // Nizar 25 Mars 2021
// simul� par lowa Hills IIR filter designer  vers 6.5:
// HPF 150 Hz ( 0.025) ,  PAPOULI 3em ordre,  fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [ 1.000000000000000000, -2.83298587828370252,  2.67927790188010251, -0.845688525879980091 ]
// b = [ 0.919744038255473129, -2.75923211476641939,  2.75923211476641939, -0.919744038255473129 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi� avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, � ne pas inverser

#define IIR_HPF_150HZ_numStages 3
const   arm_iir_lattice_instance_f32  IIR_150HZ_HPF =
{
    .numStages = IIR_HPF_150HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.845688525879980,
		 0.995236606192100,
      	-0.998039878434139
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 -0.919744038255473,
		  0.153610242753039,
		  0.010905786478106,
     	 -0.000067068506739
    }
};


// Rx Audio HPF 200 Hz IIR filter  (0.033) 3 em ordre;  Inverse Thchebychef  -60db  (2.32 ms max) // Nizar 30 Nov 2021
// simule par lowa Hills IIR filter designer  vers 6.5:
// HPF 200 Hz ( 0.033) , Inverse Thchebychef 3em ordre,  fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcule par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,    -2.792689674614181610E0,  2.606490047230018090E0,  -8.127758551611110600E-1 ]
// b = [  9.015408318882142780E-1,  -2.704436956614441280E0,  2.704436956614441280E0,  -9.015408318882142780E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes,  a ne pas inverser

#define IIR_HPF_200HZ_numStages 3
const   arm_iir_lattice_instance_f32  IIR_200HZ_HPF =
{
    .numStages = IIR_HPF_200HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.812775855161111,
		 0.991938310164061,
		-0.997252854753280
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 -0.901540831888214,
		  0.186713184157145,
		  0.016319672004627,
		 -0.000142909595327
    }
};

// Rx FM Audio LPF 2625 Hz IIR filter  3 em ordre;  Elliptic  -60db  (0.225 ms max) FS=48Khz // Nizar 04 Oct 2024
// simule par lowa Hills IIR filter designer  vers 6.5:
// LPF 2625 Hz  , Elliptic 3em ordre,  fs = 48Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcule par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,      -2.494578132221700000E0,    2.145018742337770230E0,      -6.307027153222410480E-1 ]
// b     = [  4.640776019477496740E-3,    5.228171377437046630E-3,   5.228171377437044850E-3,      4.640776019477496740E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes,  a ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_FM_2600HZ_LPF =
{
    .numStages = 3,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.630702715322241,
		  0.949299518411438,
    	 -0.972581345451989
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.004640776019477,
		0.016804949752164,
		0.027133323287463,
      	0.018004159419011
    }
};
*/




// Rx FM Audio LPF 2250 Hz IIR filter  3 em ordre;  Elliptic  -60db  (0.225 ms max) FS=48Khz // Nizar 04 Oct 2024
// simule par lowa Hills IIR filter designer  vers 6.5:
// LPF 2250 Hz (0.09630) , Elliptic 3em ordre,  fs = 48Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcule par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,      -2.470985951474843120E0,    2.086302151868523860E0,      -5.978785001558286180E-1 ]
// b     = [  4.233296162705216580E-3,    4.485553956220707410E-3,   4.485553956220705630E-3,      4.233296162705216580E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes,  a ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_FM_2600HZ_LPF =
{
    .numStages = 3,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.597878500155829,
		 0.947725505261091,
      	-0.977735950310227
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.004233296162705,
		0.014945969302698,
		0.024116143813785,
    	0.016178837403752
    }
};
*/

// Rx FM Audio LPF 2000 Hz IIR filter  3 em ordre;  Elliptic  -60db  (0.225 ms max) FS=48Khz // Nizar 04 Oct 2024
// simule par lowa Hills IIR filter designer  vers 6.5:
// LPF 2000 Hz (0.08330) , Elliptic 3em ordre,  fs = 48Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcule par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,      -2.508942076679152140E0,    2.136508410059870580E0,     -6.146108201668964990E-1 ]
// b     = [  3.504599651087561440E-3,    2.973156955823514910E-3,   2.973156955823514910E-3,     3.504599651087561440E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes,  a ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_FM_2600HZ_LPF =
{
    .numStages = 3,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.614610820166896,
		 0.955374979168789,
       	-0.982808053513199
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.003504599651088,
		0.011765994482352,
		0.018096947612923,
      	0.012203453641509
    }
};



// Rx FM Audio LPF 1680 Hz IIR filter  3 em ordre;  Elliptic  -60db  (0.225 ms max) FS=48Khz // Nizar 05 Oct 2024
// simule par lowa Hills IIR filter designer  vers 6.5:
// LPF 1680 Hz (0.06740) , Elliptic 3em ordre,  fs = 48Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcule par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,      -2.603291180246881000E0,    2.285123149112522790E0,   -6.746796868909047620E-1 ]
// b     = [  2.490030858010213510E-3,    1.086110129358476510E-3,   1.086110129358476510E-3,  2.490030858010213510E-3 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes,  a ne pas inverser

/*
const   arm_iir_lattice_instance_f32  IIR_FM_2600HZ_LPF =
{
    .numStages = 3,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.614610820166896,
		 0.955374979168789,
       	-0.982808053513199
    },

    .pvCoeffs = (float*) (const float[])
    {
    	0.003504599651088,
		0.011765994482352,
		0.018096947612923,
      	0.012203453641509
    }
}; */



// Rx FM Audio HPF 21.6 Hz IIR filter  1 em ordre;  Elliptic  -60db   FS=48Khz // Nizar 09 Oct 2024
// simule par lowa Hills IIR filter designer  vers 6.5:
// LPF 21.6 Hz (0.00090) , Elliptic 1er ordre,  fs = 48Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcule par Mathlab online 2020 avec la fonction suivante :
// format long
// a     = [  1.000000000000000000,      -9.971765562803168950E-1 ]
// b     = [  9.985882781401583590E-1,   -9.985882781401583590E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (1 coeff)  et v (2 coeff) sont correctes,  a ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_FM_22HZ_HPF =
{
    .numStages = 1,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.997176556280317
    },

    .pvCoeffs = (float*) (const float[])
    {
       -0.998588278140158,
    	0.002819457802464
    }
};


//
// 120 - 2700Hz bandpass, 1410Hz center frequency
//
const arm_iir_lattice_instance_f32 IIR_2k7_BPF =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
         0.286204893123822,
        -0.535401193365074,
         0.636552242537530,
        -0.801759238351553,
         0.632340313225578,
        -0.598674284342736,
         0.984168394137399,
        -0.996691132601798,
         0.999768009572177,
        -0.997990472561290
    },

    .pvCoeffs = (float*) (const float[])
    {
        -0.0262124858208275,
        -0.117385216786601,
        -0.223626900059669,
        -0.159085812308114,
         0.0583824351916630,
         0.119904708047547,
         0.0261315687651823,
         0.00496617536907647,
        -0.000240288448030184,
        -6.41272493413780e-06,
         1.37062857848147e-07
    }
};


