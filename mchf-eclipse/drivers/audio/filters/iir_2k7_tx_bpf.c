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
// Filter designed 20140927 by C. Turner, KA7OEI using MatLAB fdatools
//
// This filter has relaxed band-stop properties and is designed for SSB audio TX filtering.
//
// NOTE:
//	- IIR structure is Lattice Autoregressive Moving-Average (ARMA)
//	- ARM FIR/IIR algorithms require time reverse-order coefficients!!!
//
//	10th order Elliptic bandpass filter
//	Fstop:  200, 3250 Hz
//	Fpass:  300, 2700 Hz
//
//  Amplitude responses - referenced to attenuation at 1.5 kHz:
//	-6dB points:  257, 2830 Hz
//	-10dB points:  254, 2885 Hz
//	-20dB points:  246, 2988 Hz
//	-30dB points:  <225, >3060 Hz
//
//  Low-end pre-emphasis added to offset effects of Hilbert transformer:
//	>=8.0dB:   @ 280 Hz
//	>=7.0dB:  <298 Hz
//	>=6.0dB:  <355 Hz
//	>=5.0dB:  <421 Hz
//  >=4.0dB:  <521 Hz
//	>=3.0dB:  <670 Hz
//  >=2.0dB:  <870 Hz
//	>=1.0dB:  <1130 Hz
//	>=0.5dB:  <1340 Hz
//
// The above counteracts the rolloff to produce an overall flat response to approx. 1dB between 275 Hz and 2500 Hz.
//
// Pole/Zero Info:
//	1: P0.98293, 0.36082;   Z1, 0.40965
//	2: P0.99849, 0.;035664; Z1, 0.030945
//	3: P0.91702, 0.30422;   Z1, 0.53916
//	4: P0.98884, 0.038766;  Z1, 0.023267
//	5: P0.93128, 0.009238;  Z  (none)
//  Fs = 48Khz
//
#define IIR_TX_numStages     10
#define IIR_TX_2k7_numStages 10

const arm_iir_lattice_instance_f32 IIR_TX_2k7 =
{
    .numStages = IIR_TX_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
         0.6869176968793,
        -0.9512054401742,
         0.9748233658729,
        -0.9886769453802,
         0.9699856293614,
        -0.976965264625,
         0.9989051081887,
        -0.9994482291916,
         0.9999677314304,
        -0.9993834480784
    },

    .pvCoeffs = (float*) (const float[])
    {
        -0.01873226419377,
        -0.0351077314033,
        -0.01181875664072,
        -0.0100003360675,
        -0.0005123880132686,
        -0.0002827366407254,
         6.374826243116e-05,
         3.655128410718e-06,
        -1.949876071716e-08,
        -5.55178847339e-10,
         3.472732518217e-12
    }
};


// IIR filter ( version originale de l'auteur ) fs = 48Khz
// lattice ARMA coefficients designed with MATLAb fdatools
// bandpass 50 - 2750Hz   j'imagine Elliptic  1dB riples

#define IIR_TX_WIDE_BASS_numStages 10

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_BASS =
{
    .numStages = IIR_TX_WIDE_BASS_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.71896929903805900000,
		-0.95942907850625200000,
		 0.97792311678973200000,
		-0.98088701203507700000,
		 0.97297595879898900000,
		-0.99620090645682700000,
		 0.99990501182910600000,
		-0.99996667856688500000,
		 0.99998653853233900000,
		-0.99998543366088200000

    },
    .pvCoeffs = (float*) (const float[])
    {
    	-0.00032448877846164900,
		-0.00117728102231410000,
		-0.00209099021086666000,
		-0.00223337754782698000,
		-0.00122619886193943000,
		-0.00046485618062794300,
		 0.00001256926158546300,
		 0.00000019091112216183,
		-0.00000000006469610177,
		-0.00000000000289012694,
		 1.24E-15
    }
};

// Designer By Nizar NIZZZZ  le 24/02/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2750Hz (0.1144) ELLIPTIC  -80db  10em ordre, ripples 0.000dB  fs = 48K   (2.14 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [1.0000000000000        , -8.64347750464988529   , 33.9452604694905702   , -79.7256452069872701    , 123.959687521233675  , -133.276288028993983  , 100.319742800902234  , -52.1895393680751152   , 17.9549301501776126   , -3.68809546512208719   , 0.34342821242880115    ]
// b = [0.000186659944204848216, -0.00118088864424422257, 0.00361243442310370488,  -0.00709871195245101738, 0.0101640434017928727, -0.0113634939402624968, 0.0101640434017928705, -0.00709871195245101916, 0.00361243442310370488, -0.00118088864424422235, 0.000186659944204848216]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,
/*
 #define IIR_TX_WIDE_WIDE_numStages 10

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE =
{
    .numStages = IIR_TX_WIDE_WIDE_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.343428212428801,
		-0.815912560304185,
		 0.943769365686666,
		-0.968072879022351,
		 0.975826199899304,
		-0.978700176443930,
		 0.983281531307118,
		-0.973558320304976,
		 0.996155657630353,
    	-0.947046285240918
     },
    .pvCoeffs = (float*) (const float[])
    {
    	 0.186659944204848E-3,
		 0.432502384509586E-3,
		 0.893348263832282E-3,
		 0.945279911215580E-3,
		 0.809516598582562E-3,
		 0.420479215175981E-3,
		 0.209944889164865E-3,
		 0.054818593470661E-3,
		 0.017190950224751E-3,
		 0.001376189753814E-3,
		 0.000113096629441E-3,
     }
};

*/




// Designer By Nizar NIZZZZ  le 07/04/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2850Hz (0,474260) Papouli   10em ordre,  fs = 12K   (2.00 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.000000000000000000000, -2.6004916205384538400,  5.0926737861215682900, -6.7760429272857027900,  7.0863247845269912200, -5.741850688361663120, 3.6592646626575482400, -1.7883086870091105000, 0.6419883193429466670, -0.15314892589480479900,  0.018587175133716815600 ]
// b = [ 0.000428706912786169969,  0.00428706912786170147, 0.0192918110753776539,  0.0514448295343404194,  0.0900284516850957495,  0.108034142022114898, 0.0900284516850957495,  0.0514448295343404194, 0.0192918110753776539,  0.00428706912786170236,  0.000428706912786169969 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

/*
 #define IIR_TX_WIDE_WIDE_DECIM_numStages 10

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = IIR_TX_WIDE_WIDE_DECIM_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.018587175133717,
		-0.104849356388617,
		 0.278121013749539,
		-0.463883061349671,
		 0.591349283089589,
		-0.639465020725837,
		 0.739785504897523,
		-0.542410876057987,
		 0.941318781069811,
    	-0.207583646484353
     },
    .pvCoeffs = (float*) (const float[])
    {
    	 0.000428706912786,
		 0.005401917862229,
		 0.031145661208545,
		 0.106919634876858,
		 0.229783780207992,
		 0.287912690865325,
		 0.160980025132940,
		-0.032649498159498,
		-0.070012765953113,
		 0.006909064017325,
		 0.020689100026982
     }
};

*/


// Designer By Nizar NIZZZZ  le 18/04/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2700Hz (0.4500) Papouli    10em ordre,  fs = 12K   (2.03 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.000000000000000000,  -3.10871732065926842,  6.35282602609539548,  -8.93979070357003636, 9.57783066299014685, -7.899865717559023, 5.04942898169882071, -2.45019540546032744, 0.862676254226554207, -0.199233206844615873, 0.0230374502946704185]
// b = [ 0.00026171584102765193,  0.00261715841027652063, 0.0117772128462443448,  0.031405900923318244,  0.0549603266158069204, 0.0659523919389683133, 0.0549603266158069204, 0.031405900923318244, 0.0117772128462443426, 0.00261715841027652063, 0.00026171584102765193]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

/*

 #define IIR_TX_WIDE_WIDE_DECIM_numStages 10

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = IIR_TX_WIDE_WIDE_DECIM_numStages,
    .pkCoeffs  = (float*) (const float[])
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
    .pvCoeffs = (float*) (const float[])
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
		   0.017364552059658
     }
};
*/




// Designer By Nizar NIZZZZ  le 20/04/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2750Hz (0.4576) Papouli    10em ordre,  fs = 12K   (2.02 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.00000000000000000000, -2.95058442631758533   ,  5.93807742315147991 , -8.2187215545792256, 8.73406369863777066, -7.16459041322603163, 4.57390783065046858, -2.22394802455078633 , 0.787515425855732332,  -0.183643265126889976, 0.0215435609495889357]
// b = [ 0.00030626978070754225,  0.00306269780707542294, 0.0137821401318394088,  0.0367523736849050842,  0.0643166539485839017,   0.0771799847383006821,  0.0643166539485838928, 0.0367523736849050842, 0.0137821401318394066, 0.00306269780707542294, 0.00030626978070754225]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

/*
 #define IIR_TX_WIDE_WIDE_DECIM_numStages 10

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = IIR_TX_WIDE_WIDE_DECIM_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.021543560949589,
		-0.120132926398283,
		 0.310220010050171,
		-0.501001658290637,
		 0.621747792566843,
		-0.670067800355853,
		 0.748187355490353,
		-0.596509250215580,
		 0.935532741490501,
    	-0.269438603638927
       },
    .pvCoeffs = (float*) (const float[])
    {
    	   0.000306269780708,
		   0.003966372652283,
		   0.023656338504973,
		   0.084630463038490,
		   0.190964250093684,
		   0.256260304733339,
		   0.256260304733339,
		   0.167474922804612,
		  -0.004463802165585,
		  -0.062066429514362,
		  -0.000381652160600,
    	   0.018814700350441
    }
};
*/

// Designer By Nizar NIZZZZ  le 20/04/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2800Hz (0.466) Elleptic    10em ordre, 0db rip , -80db   fs = 12K   (3.01 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.00000000000000000000,  -1.85975319109059312, 3.95567888047550031, -4.5510038987523922,   4.91165092316276652,    -3.68859959554152095,  2.38502162168795584,  -1.12071775023083031,  0.408237524392387385, -0.0966297305925191985,   0.0122102504901905617 ]
// b = [ 0.00756757130637454889,  0.0334823182819604659, 0.0899441325754735566, 0.168955864040339465, 0.24211551308907584,  0.271964235414496702, 0.242115513089075884,   0.168955864040339465, 0.0899441325754735566, 0.0334823182819604659,  0.00756757130637454889]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,


/*

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.012210250490191,
		-0.073932700921798,
		 0.223784748238098,
		-0.384398771897832,
		 0.602981184368225,
		-0.484937542558251,
		 0.886041411955346,
		-0.278396670915914,
		 0.989296215201702,
    	-0.140917637719722
     },
    .pvCoeffs = (float*) (const float[])
    {
    	  0.007567571306375,
		  0.047556133167796,
		  0.148408990325728,
		  0.288902846695565,
		  0.350275107774587,
		  0.205107958281735,
		 -0.033304378423410,
		 -0.073789835417632,
		 -0.020653117364821,
		  0.004808828889240,
		  0.007164517177795
     }
};

*/


// Designer By Nizar NIZZZZ  le 12/08/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2800Hz (0.466) Elleptic    10em ordre, 0db rip , -64db   fs = 12K   (3.83 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.00000000000000000000,   -1.579461367100089220E0,  3.698914397928254160E0, -3.872580873393324910E0,  4.487994433699216580E0, -3.177567181415119180E0,    2.182869560963417670E0,   -9.893832520512916060E-1, 3.822956879276611540E-1,  -8.903925953177276750E-2,  1.207776500254175820E-2 ]
// b = [ 1.624045820396923470E-2,   5.759391837957216430E-2, 1.448194632061898800E-1, 2.553024136155752630E-1, 3.563520607075488120E-1,  3.955032838037829460E-1,  3.563520607075488120E-1,  2.553024136155752630E-1,  1.448194632061899020E-1,  5.759391837957216430E-2,  1.624045820396923470E-2]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,


/*

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	  0.012077765002542,
		 -0.069973103454403,
		  0.228327593385698,
		 -0.348975648656558,
		  0.654416565532478,
		 -0.383086802583378,
		  0.932094294200926,
		 -0.205223073218317,
		  0.994401244724010,
    	 -0.126078208052115
      },
    .pvCoeffs = (float*) (const float[])
    {
    	  0.016240458203969,
		  0.083245094696745,
		  0.216159457614461,
		  0.348397218514116,
		  0.333275225577833,
		  0.116524715285881,
		 -0.100913729353676,
		 -0.055682546714472,
		  0.000152618028695,
		  0.002886750491023,
    	  0.003480416814102
      }
}; */

// Designer By Nizar NIZZZZ  le 02/02/2022 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2770Hz (0.4616) Elleptic    10em ordre, 0db rip , -64db   fs = 12K   (3.84 ms max  @ 2760Hz )
// pour mieux coincider 2760 avec une harmonique de 275Hz et 690Hz  des filtres parametriques par défaut
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [  1.000000000000000000,     -1.682958867976065860E0, 3.842112997289413020E0,   -4.165200013173916640E0, 4.746091304045767960E0, -3.426728394575733640E0, 2.322072594619833290E0,  -1.062298013038154740E0, 4.049704033990746410E-1, -9.433120390410126180E-2,  1.257555501326214300E-2  ]
// b = [  1.567171928084960750E-2,  5.390858268655200010E-2,  1.346293804879421520E-1,  2.352414464137643440E-1, 3.273602298708606200E-1, 3.626836442194438350E-1, 3.273602298708606640E-1, 2.352414464137643440E-1, 1.346293804879421520E-1, 5.390858268655200010E-2, 1.567171928084960750E-2  ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,


/*

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.140051304365811,
		  0.012575555013262,
		 -0.073178634879574,
		  0.234878624283912,
		 -0.362111086087920,
		  0.657952286632417,
		 -0.400554687375480,
		  0.931666124674085,
		 -0.220920203432308,
	      0.994378366141950
      },
    .pvCoeffs = (float*) (const float[])
    {
    	  0.003591502614796,
		  0.015671719280850,
		  0.080283441626689,
		  0.209456712389953,
		  0.341133489132136,
		  0.332700863336379,
		  0.124355511917604,
		 -0.092307655082995,
		 -0.055171310553157,
		 -0.002842560402745,
		  0.002588975053145
      }
};
*/

//***************************************************************************************************************

// Designer By Nizar NIZZZZ  le 02/02/2022 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2760Hz (0.4607) Elleptic    10em ordre, 0db rip , -64db   fs = 12K   (3.85 ms max  @ 2755Hz )
// pour mieux coincider 2755 avec une harmonique de 275Hz et 690Hz  des filtres parametriques par défaut
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [  1.000000000000000000,      -1.704104796584337000E0, 3.872445736990436590E0,   -4.226226564630264800E0, 4.800998753126807910E0, -3.479163497471592150E0, 2.351693659831265660E0,  -1.077672458717556130E0,  4.097760220737047380E-1, -9.544262428075523270E-2,  1.268024240954146450E-2  ]
// b = [  1.555779471387231720E-2,  5.317638724369350810E-2,   1.326239777257543380E-1,  2.313073346115126050E-1, 3.216917479059894890E-1, 3.562699883456037500E-1, 3.216917479059895340E-1, 2.313073346115126050E-1, 1.326239777257543380E-1, 5.317638724369350810E-2,  1.555779471387231720E-2  ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,


/*

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
 	     0.012680242409541,
	    -0.073846035965670,
	     0.236246545418413,
	    -0.364783891728366,
	     0.658695769571344,
	    -0.404084002739600,
	     0.931580727137725,
	    -0.224122269218813,
	     0.994373823666735,
	    -0.142905707821476

      },
    .pvCoeffs = (float*) (const float[])
    {
		  0.015557794713872,
		  0.079688499839878,
		  0.208100197552107,
		  0.339632409168967,
		  0.332510382389347,
		  0.125886612325012,
		 -0.090538739652460,
		 -0.055034587269957,
		 -0.003438922287773,
		  0.002527101055738,
		  0.003610640917937
       }
};

*/

//***************************************************************************************************************

// Designer By Nizar NIZZZZ  le 04/02/2022 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2710Hz (0.4515) Elleptic    10em ordre, 0db rip , -64db   fs = 12K   (3.85 ms max  @ 2700Hz )
// pour mieux coincider 2700 Hz avec une harmonique de 270Hz et 675Hz  des filtres parametriques par défaut
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [  1.000000000000000000,      -1.919775363553691600E0, 4.202653505237439190E0,   -4.874219036899391180E0, 5.404603507182516250E0, -4.045662215455743120E0,  2.677789048626624260E0,   -1.244367936296710830E0,  4.623103633809285460E-1, -1.074007219296950530E-1,  1.380849484136744020E-2  ]
// b = [  1.443845131259236190E-2,  4.609463442629390780E-2,  1.135790526249945030E-1,  1.941815942882543440E-1, 2.685051036701131100E-1, 2.961419724891489750E-1,  2.685051036701131100E-1, 1.941815942882543000E-1,  1.135790526249945030E-1, 4.609463442629390780E-2,  1.443845131259236190E-2  ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,


/*

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {

		 0.013808494841367,
		-0.080906940618338,
		 0.250763865979458,
		-0.391831283297120,
		 0.666666601217656,
		-0.439300911040690,
		 0.930754283447795,
		-0.256669832226027,
	     0.994330147212783,
		-0.172003676854432

     },
    .pvCoeffs = (float*) (const float[])
    {

		 0.014438451312592,
		 0.073813217544078,
		 0.194521576798272,
		 0.324032382310797,
		 0.329197859034879,
		 0.140122281568956,
		-0.072437619891997,
		-0.053043997999241,
		-0.009168855312577,
		 0.001883851379758,
		 0.003736478903420
     }
};

*/
//************************************************************************************************************

// Designer By Nizar NIZZZZ  le 09/04/2022 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// -35 db a 3.00 Khz
// LPF  0Hz .. 2800Hz (0.4666) Elleptic    10em ordre, 0db rip , -72db   fs = 12K   (3.34 ms max  @ 2783Hz )
// on a été jusqua 2800 Hz pour compenser les attenuations du bord des filtres Hilbert et autres et éviter les retards de propagation de phase
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [  1.000000000000000000,     -1.728100711715971460E0,  3.824590599239054530E0,  -4.229616807626073620E0, 4.694373049377257520E0, -3.446436488115443450E0, 2.280463137744908500E0, -1.059024290927874420E0,  3.944450400127727630E-1,-9.316456954847367910E-2, 1.209785358546598080E-2  ]
// b = [  1.093701347320386710E-2,  4.355773028836310350E-2,  1.127519512551451000E-1,   2.053791941906904840E-1, 2.901453717044932380E-1, 3.240842902018306490E-1, 2.901453717044932820E-1,  2.053791941906904840E-1,  1.127519512551451000E-1, 4.355773028836311230E-2, 1.093701347320386710E-2  ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

/*
const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.012097853585466,
		-0.072268837284117,
		 0.224574930583175,
		-0.370173271157004,
		 0.623667809932443,
		-0.439630367418459,
		 0.909526431078672,
		-0.240966004831426,
		 0.992072791101545,
    	-0.132896920794833
    },
    .pvCoeffs = (float*) (const float[])
    {

    	0.010937013473204,
		0.062457991055454,
		0.178801444252121,
		0.318954118690842,
		0.347847051094679,
	    0.165499654211565,
	   -0.068365431286252,
	   -0.066625986321438,
	   -0.009601795592966,
		0.003905016492656,
	  	0.005220312059036
    }
};
*/

//**************************************************************************************************************
// Designer By Nizar NIZZZZ  le 13/04/2023 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// -50 db a 3.00 Khz
// LPF  0Hz .. 2800Hz (0.4666) Elleptic    10em ordre, 0.010db rip , -72db   fs = 12K   (4.48 ms max  @ 2783Hz )
// on a été jusqua 2800 Hz pour compenser les attenuations du bord des filtres Hilbert et autres et éviter les retards de propagation de phase
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [  1.000000000000000000,     -2.100725077677215950E0,  4.657339114803538570E0,    -5.898219274544476140E0 ,     6.762527761285251240E0, -5.571648999770991040E0, 3.893750655926368150E0, -2.025596663829195790E0, 8.164625887696836060E-1,  -2.196222936377872250E-1,   3.249468412832579030E-2 ]
// b = [   1.033016887507027580E-2,   3.731007024952409880E-2,   9.435761744077657550E-2,  1.672858261398156630E-1,   2.340689733986578870E-1, 2.600571832458130390E-1,   2.340689733986578870E-1,  1.672858261398156190E-1, 9.435761744077657550E-2, 3.731007024952409880E-2,  1.033016887507027580E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,



const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.032494684128326,
		-0.151519886327511,
		 0.356454860959664,
		-0.495223910466974,
		 0.702350264527633,
		-0.479051088759495,
		 0.933763634417664,
		-0.243636887871763,
		 0.994739628820369,
    	-0.132252445587560
    },
    .pvCoeffs = (float*) (const float[])
    {
    	0.010330168875070,
		0.059010915062025,
		0.169921681402685,
		0.301602977264816,
		0.313482027370393,
		0.130958823061347,
	   -0.064434884533329,
	   -0.045685420573993,
	   -0.003147256341809,
		0.002219508064841,
       	0.002749733106040
    }
};


//**************************************************************************************************************
//**************************************************************************************************************
//**************************************************************************************************************
// Designer By Nizar NIZZZZ  le 30/05/2024 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// -20 db a 3.00 Khz
// LPF  0Hz .. 2900Hz (0.4826) Elleptic    10em ordre, 0.00db rip , -70db   fs = 12K   (3.43 ms max  @ 2883Hz )
// on a été jusqua 2900 Hz pour compenser les attenuations du bord des filtres Hilbert et autres et éviter les retards de propagation de phase
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a     = [  1.000000000000000000,     -1.304823712526955280E0,    3.294513474870216290E0,     -3.108692701775626510E0,    3.758823095151131000E0,    -2.525910390371798100E0,    1.781962291826968190E0,   -7.947864860971844080E-1,  3.137004668284829380E-1,  -7.418134039761287820E-2,   1.035234863898613520E-2 ]
// b     = [  1.395153675123216090E-2,   5.936297301032028350E-2,   1.575245662130375420E-1,     2.928124647195746280E-1,   4.176864310154255300E-1,    4.682811027274266460E-1,   4.176864310154256190E-1,   2.928124647195746280E-1,  1.575245662130375420E-1 ,  5.936297301032029240E-2,   1.395153675123216090E-2 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

/*
const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE_DECIM =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.010352348638986,
		-0.060679853541125,
		 0.201227015297186,
		-0.317922597441479,
		 0.615356380928256,
		-0.359875060974211,
		 0.917759909948956,
		-0.170371461675787,
		 0.992883781714058,
     	-0.077839619966069
    },
    .pvCoeffs = (float*) (const float[])
    {
    	 0.013951536751232,
		 0.077567268989519,
		 0.212723926155032,
		 0.355697776361272,
		 0.349653441655168,
		 0.122710597862061,
		-0.112927046543045,
		-0.065851471820001,
		 0.006722279402669,
		 0.005061936040622,
     	 0.003931725316125
   }
};
*/
//**************************************************************************************************************
//**************************************************************************************************************

// Designer By Nizar NIZZZZ  le 05/05/2024 moyennant le logiciel lowa Hills IIR filter designer ver 6.5

// Filtre passe tout   137 Hz  (0.0228) Bessel    10em ordre    fs = 12K   (8.35 ms for freq < 300 Hz )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a     = [  1.000000000000000000,      -8.916367199297814850E0,    3.581820410372080770E1,    -8.536516041476561510E1,  1.336661090218998590E2,  -1.436777901913540490E2,  1.073668695881951680E2,  -5.507564061012334200E1,  1.855984927623825610E1,  -3.710163173265657480E0,  3.340896025434725300E-1   ]
// b     = [  3.340896025434725300E-1,   -3.710163173265657030E0,    1.855984927623825610E1,    -5.507564061012334200E1,  1.073668695881951680E2,  -1.436777901913540710E2,  1.336661090218998590E2,  -8.536516041476559740E1,  3.581820410372080770E1 , -8.916367199297814850E0,  1.000000000000000000E0 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

/*
const  arm_iir_lattice_instance_f32   IIR_TX_PASS_TOUT_137HZ =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	 	 0.334089602543473,
			-0.823177236821566,
			 0.956574033981359,
			-0.981216230918109,
			 0.989015060680330,
			-0.992512075040795,
			 0.994430473411610,
			-0.995728565421908,
			 0.996903443340637,
			-0.998274090121911
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 	 	 	 	 	 1,
	       	   	   	     	 0,
							 0,
							 0,
							 0,
							 0,
							 0,
							 0,
							 0,
							 0,
							 0
    }
};

*/

//**************************************************************************************************************
//**************************************************************************************************************
//**************************************************************************************************************
// Designer By Nizar NIZZZZ  le 06/05/2024 moyennant le logiciel lowa Hills IIR filter designer ver 6.5

// Filtre passe tout   137 Hz  (0.0228) Bessel    12 em ordre    fs = 12K   (9.25 ms for freq < 350 Hz )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a     = [  1.000000000000000000,      -1.060578302920963220E1,     5.161467309488980250E1,    -1.524110315575778470E2,   3.041241102311472490E2,   -4.320173721908711960E2,   4.479691075034627270E2,   -3.416351215892707850E2,  1.901764123554142700E2,   -7.535864431558706220E1,  2.017670544389657210E1,  -3.277274547773812770E0,  2.442186016723395080E-1   ]
// b     = [  2.442186016723395080E-1,   -3.277274547773812770E0,     2.017670544389657210E1,    -7.535864431558706220E1,   1.901764123554142700E2,   -3.416351215892707850E2,   4.479691075034627270E2,   -4.320173721908711960E2,  3.041241102311472490E2 ,  -1.524110315575778470E2,  5.161467309488980250E1,  -1.060578302920963220E1,  1.0  ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,


const  arm_iir_lattice_instance_f32   IIR_TX_PASS_TOUT_137HZ =
{
    .numStages = 12,
    .pkCoeffs  = (float*) (const float[])
    {
    	  0.244218601672339,
		 -0.730727634381666,
		  0.927244473486499,
		 -0.969319022450361,
		  0.982427864470258,
		 -0.988235354614745,
		  0.991327845093873,
		 -0.993197631797895,
		  0.994497134997881,
		 -0.995607853634083,
		  0.996796192451550,
       	 -0.998239957881353
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 1,
    	 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0
    }
};


//**************************************************************************************************************

//**************************************************************************************************************
// Designer By Nizar NIZZZZ  le 13/06/2026 moyennant le logiciel lowa Hills IIR filter designer ver 6.5

// Filtre passe tout   116 Hz  (0.0194) Bessel    12 em ordre    fs = 12K   (10.84 ms for freq < 250 Hz )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = 1, -10.8112555519859, 53.6160850777101, -161.281948393867, 327.740583280073, -473.976373703036, 500.206311530136, -388.134305062421, 219.771089086276, -88.556497002876, 24.1042191999265, -3.97920208264158, 0.301293622736948
// b = 0.301293622736948, -3.97920208264158, 24.1042191999265, -88.556497002876, 219.771089086276, -388.134305062421, 500.206311530136, -473.976373703036, 327.740583280073, -161.281948393867, 53.6160850777101, -10.8112555519859, 1
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

/*
const  arm_iir_lattice_instance_f32   IIR_TX_PASS_TOUT_137HZ =
{
    .numStages = 12,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.301293622736948,
    	-0.793909088937193,
    	 0.948111218610414,
    	-0.977922980216304,
    	 0.987307534557095,
    	-0.991491953452496,
    	 0.993725008520733,
    	-0.995077109393249,
    	 0.996019299677756,
    	-0.996821995099226,
    	 0.997688169811196,
    	-0.998730907937112
    },

    .pvCoeffs = (float*) (const float[])
    {
    	 1,
    	 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0
    }
};
*/

//**************************************************************************************************************
// Designer By Nizar NIZZZZ  le 06/05/2024 moyennant le logiciel lowa Hills IIR filter designer ver 6.5

// Filtre passe tout   75 Hz  (0.0125) Bessel    10 em ordre    fs = 12K   (15.25 ms for freq < 175 Hz )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a     = [  1.000000000000000000,      -9.402339940217700590E0,    3.979546919972829500E1,    -9.984701560129591870E1,   1.644567840694411530E2,   -1.858044119001597940E2,   1.458278617471141960E2,  -7.850718206639769820E1,  2.774516161112567940E1,    -5.812453853015018270E0,  5.481267336887029270E-1 ]
// b     = [  5.481267336887029270E-1,   -5.812453853015019160E0,    2.774516161112568380E1,    -7.850718206639769820E1,   1.458278617471141960E2,   -1.858044119001597940E2,   1.644567840694411530E2,  -9.984701560129591870E1,  3.979546919972828610E1 ,   -9.402339940217698810E0,  9.999999999999998220E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

/*
const  arm_iir_lattice_instance_f32   IIR_TX_PASS_TOUT_137HZ =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {
    	  0.548126733688703,
		 -0.941710101710817,
		  0.987334950304367,
		 -0.994402279643503,
		  0.996709037174648,
		 -0.997752964411311,
		  0.998326423358296,
		 -0.998724990180312,
		  0.999066892564581,
       	 -0.999487881219645,
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 1,
    	 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0
    }
};
*/



//**************************************************************************************************************
//**************************************************************************************************************
// Designer By Nizar NIZZZZ  le 06/05/2024 moyennant le logiciel lowa Hills IIR filter designer ver 6.5

// Filtre passe tout   182 Hz  (0.0125) Bessel    12 em ordre    fs = 12K   (6.16 ms for freq < 250 Hz )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = 1, -9.66323301207516, 42.9189250197991, -115.848464939241, 211.646661204282, -275.691416194003, 262.538373696764, -184.152563024894, 94.4228385155273, -34.512799493076, 8.53557474074377, -1.28240959463709, 0.08851311355811
// b = 0.08851311355811, -1.28240959463709, 8.53557474074378, -34.5127994930761, 94.4228385155273, -184.152563024894, 262.538373696764, -275.691416194003, 211.646661204282, -115.848464939241, 42.9189250197991, -9.66323301207516, 1
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (12 coeff)  et v-coeff (13 coeff) ,


const  arm_iir_lattice_instance_f32   IIR_TX_PASS_TOUT_182HZ =
{
    .numStages = 12,
    .pkCoeffs  = (float*) (const float[])
    {
    	     0.08851311355811,
			-0.430459217116889,
			 0.774345860142964,
			-0.905705843248658,
			 0.94805249270881,
			-0.96616573026782,
			 0.975865143216011,
			-0.981931185071176,
			 0.986306471879698,
			-0.989936203308606,
			 0.993304156142396,
			-0.996632721065733
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 1,
    	 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0
    }
};


//**************************************************************************************************************
//**************************************************************************************************************
// Designer By Nizar NIZZZZ  le 18/07/2025 moyennant le logiciel lowa Hills IIR filter designer ver 6.5

// Filtre passe tout   278 Hz  (0.0463) Gaussian    8 em ordre    fs = 12K   (3.25 ms for freq < 500 Hz )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = 1, -9.66323301207516, 42.9189250197991, -115.848464939241, 211.646661204282, -275.691416194003, 262.538373696764, -184.152563024894, 94.4228385155273, -34.512799493076, 8.53557474074377, -1.28240959463709, 0.08851311355811
// b = 0.08851311355811, -1.28240959463709, 8.53557474074378, -34.5127994930761, 94.4228385155273, -184.152563024894, 262.538373696764, -275.691416194003, 211.646661204282, -115.848464939241, 42.9189250197991, -9.66323301207516, 1
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : k-coeff (8 coeff)  et v-coeff (9 coeff),

/*
const   arm_iir_lattice_instance_f32    IIR_RX_PASS_TOUT_278HZ =
{
    .numStages =  8,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.116305132382205,
    	-0.504008955227552,
    	 0.821534238657363,
    	-0.92528223748,
    	 0.958518740823159,
    	-0.973924351610793,
    	 0.983772965196204,
    	-0.991974659228602
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 1,
    	 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0,
		 0
    }
};

*/

//**************************************************************************************************************
//**************************************************************************************************************
// Designer By Nizar NIZZZZ  le 24/02/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2750Hz ( 0.1144) ELLIPTIC  -70db  10em ordre, ripples 0.040dB  fs = 48K   ( 4.16 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.000000000000000000000, -8.95998204103202944    , 36.4876962944427818  , -88.894893601783096  , 143.437878866318402  , -160.126499304985725  , 125.220369134770548  , -67.7222076020624897  , 24.2382904140893007  , -5.18364429742882038   , 0.502994395415314077]
// b = [ 0.000486650095198420551, -0.003503385858581077090, 0.0120202041052857012, -0.026067024253912785, 0.0400183354097275767, -0.0459073012512515799, 0.0400183354097275767,  -0.026067024253912785, 0.0120202041052857012, -0.00350338585858107709, 0.000486650095198420551]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) .
/*


const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE =
{
    .numStages = 10,
    .pkCoeffs  = (float*) (const float[])
    {

    	 0.502994395415314,
		-0.906059697157623,
		 0.966807028332349,
		-0.976876322681551,
		 0.980630251313873,
		-0.979400318900061,
		 0.989346246566274,
		-0.963268832634426,
		 0.999038157778843,
   	    -0.941004801826448
     },
    .pvCoeffs = (float*) (const float[])
    {

    	 0.000486650095198,
		 0.000856990254663,
		 0.001551513254723,
		 0.001155521038267,
		 0.000836755461188,
		 0.000296077181395,
		 0.000131301137964,
		 0.000019322046973,
		 0.000005462654817,
		 0.000000192586820,
     	 0.000000003608639
    }
};

*/

// Designer By Nizar NIZZZZ  le 26/02/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2800Hz (0.1165) ELLIPTIC  -80db  10em ordre, ripples 0.000dB  fs = 48K   (2.10 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.00000000000000       , -8.61356116819247575   , 33.7240896238491406   , -78.99208225047051     , 122.528346336048699   , -131.467022436078684  ,  98.7844197663128654   , -51.3154765289517911    , 17.6332322178619472    , -3.61868524707367634   , 0.336743944172269494]
// b = [ 0.000190770289169253227, -0.00119138752213924937, 0.00360579069528674534,  -0.0070219148932573674, 0.00998984440844897925, -0.0111419484772181865,  0.00998984440844897925,  -0.00702191489325736562,  0.00360579069528674445, -0.00119138752213924937, 0.000190770289169253227]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

 #define IIR_TX_WIDE_WIDE_numStages 10

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE =
{
    .numStages = IIR_TX_WIDE_WIDE_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
     	 0.336743944172269,
		-0.809968235989002,
		 0.941606610972604,
		-0.966873403349911,
		 0.974926747715973,
		-0.977903502852863,
		 0.982679170138250,
		-0.972534498194130,
		 0.996032198685120,
    	-0.945063883105291
     },
    .pvCoeffs = (float*) (const float[])
    {
    	 0.000190770289169,
		 0.000451824032694,
		 0.000940814445965,
		 0.001016857708303,
		 0.000880261376683,
		 0.000466528121706,
		 0.000234566810750,
		 0.000062493286399,
		 0.000019560309094,
		 0.000001598387724,
    	 0.000000118197553
    }
};






/*

// Designer By Nizar NIZZZZ  le 22/02/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// LPF  0Hz .. 2750Hz (0.1144) inv chebyshev  -60db  10em ordre  fs = 48K  (0.98 ms max)
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [1.00000000000000000000, -7.39003918779464186   , 24.8532137037203338  , -50.024389869192305   , 66.6688922925126093  , -61.4231356235342929 , 39.5926262749977198  , -17.6210894137308038  , 5.17970625589336464  , -0.907690336678061271  , 0.0719834420782342832]
// b = [0.00141258102753470149, -0.00773781414754216357, 0.0207198660626992748, -0.0358991044792171365, 0.0465702723388418249, -0.050054063332499501, 0.0465702723388418249, -0.0358991044792171365, 0.0207198660626992748, -0.00773781414754216357, 0.0014125810275347015]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (10 coeff)  et v-coeff (11 coeff) ,

 #define IIR_TX_WIDE_WIDE_numStages 10

const arm_iir_lattice_instance_f32 IIR_TX_WIDE_WIDE =
{
    .numStages = IIR_TX_WIDE_WIDE_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	0.071983442078234,
       -0.377686907344509,
    	0.731897631673019,
       -0.885290856885324,
        0.936395523581123,
       -0.956645724307652,
        0.971060401196620,
       -0.966502960231844,
        0.992502371242150,
       -0.947025406119087,
    },
    .pvCoeffs = (float*) (const float[])
    {
    	0.001412581027535,
    	0.002701215001875,
    	0.005501334147796,
    	0.007278039672716,
    	0.007895379922467,
    	0.005163207668332,
    	0.003050507740898,
    	0.001024754876930,
    	0.000361085702532,
    	0.000043064889191,
        0.000004649270358
    }
};
*/







// Designer By Nizar NIZZZZ  le 24/03/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// HPF  74.4Hz (0.0031) Papouli   3em ordre,   fs = 48K   (6.24 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.000000000000000000, -2.9792352845143899,  2.95867738844521888, -0.979440867687170602 ]
// b = [ 0.989669192580847401, -2.9690075777425422,  2.96900757774254220, -0.989669192580847401 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (3 coeff)  et v-coeff (4 coeff) ,

#define IIR_TX_HPF_75HZ_numStages 3

const arm_iir_lattice_instance_f32 IIR_TX_HPF_75HZ =
{
    .numStages = IIR_TX_HPF_75HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.979440867687171,
		 0.999926518617022,
    	-0.999969933335144
    },
    .pvCoeffs = (float*) (const float[])
    {
    	-0.989669192580847,
		 0.020550199208815,
		 0.000201937041318,
    	-0.000000018305811
    }
};


// Designer By Nizar NIZZZZ  le 07/04/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// HPF  85Hz (0,01416) Papouli   3em ordre,   fs = 12K   (5.45 ms max )
// calcule par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.000000000000000000, -2.90528382879412783,  2.81486035848870753,  -0.909462940903690154 ]
// b = [ 0.953700891023315656, -2.86110267306994714,  2.86110267306994714,  -0.953700891023315656 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copies avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (3 coeff)  et v-coeff (4 coeff) ,
/*
#define IIR_TX_HPF_85HZ_DECIM_numStages 3

const arm_iir_lattice_instance_f32 IIR_TX_HPF_85HZ_DECIM =
{
    .numStages = IIR_TX_HPF_85HZ_DECIM_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.909462940903690,
		  0.998468416935543,
    	 -0.999372213743034
     },
    .pvCoeffs = (float*) (const float[])
    {
    	 -0.953700891023316,
		  0.090330896873357,
		  0.003842273264314,
    	 -0.000007412532765
    }
};
*/




// Tx Audio HPF 65 Hz IIR filter  (0.01084) 3em ordre;  PAPOULI  3 em ordre// Nizar 11 Mars 2022
// simule par lowa Hills IIR filter designer  vers 6.5:
// HPF 65 Hz ( 0.01084) ,  PAPOULI 3em ordre,  fs = 12Khz
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcules par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -2.927461918840513990E0,   2.857443639824655430E0,  -9.299302017835707090E-1 ]
// b = [  9.643544700560923390E-1,  -2.893063410168277280E0,  2.893063410168277280E0,   -9.643544700560923390E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, a ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_TX_HPF_65HZ_DECIM =
{
    .numStages = 3,
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



// Designer By Nizar NIZZZZ  le 07/04/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// HPF  75Hz (0,0125) Papouli   3em ordre,   fs = 12K   (6.16 ms max )
// calcule par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.000000000000000000, -2.9163707272893804,  2.83608940129911824, -0.919640106965414006 ]
// b = [ 0.959012529444239092, -2.87703758833271728, 2.87703758833271728, -0.959012529444239092 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copies avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (3 coeff)  et v-coeff (4 coeff) ,

#define IIR_TX_HPF_75HZ_DECIM_numStages 3

const arm_iir_lattice_instance_f32 IIR_TX_HPF_75HZ_DECIM =
{
    .numStages = IIR_TX_HPF_75HZ_DECIM_numStages,
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


// Tx Audio HPF 80 Hz IIR filter  (0.01334) 3em ordre;  PAPOULI  4 em ordre// Nizar 10 juillet 2022
// simule par lowa Hills IIR filter designer  vers 6.5:
// HPF 80 Hz ( 0.01334) ,  PAPOULI 4em ordre,  fs = 12Khz  (max 8.91 ms)
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcules par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -3.880641768669832460E0,  5.648998100796940310E0,  -3.655826185477547780E0 , 8.874749188356696900E-1 ]
// b = [  9.420588108612495400E-1,  -3.768235243444998160E0,  5.652352865167496350E0,  -3.768235243444998160E0,  9.420588108612495400E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie  avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (3 coeff)  et v (4 coeff) sont correctes, a ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_TX_HPF_80HZ_DECIM =
{
    .numStages = 4,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.887474918835670,
		-0.997484222435761,
		 0.999490398529692,
     	-0.999466482951945
     },

    .pvCoeffs = (float*) (const float[])
    {
    	  0.942058810861250,
		 -0.112442473473400,
		 -0.006145701469376,
		  0.000013865239847,
    	  0.000002078385743
    }
};

// Tx Audio HPF 90 Hz IIR filter  (0.015) 1er  ordre;  inverse tchebechef  1er  ordre// Nizar 12 Avril 2025
// simule par lowa Hills IIR filter designer  vers 6.5:
// HPF 90 Hz ( 0.015) ,  Inverse Tchebetchef 1er ordre,  fs = 12Khz  (max 1.76 ms)
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcules par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -9.539525559077633330E-1 ]
// b = [  9.769762779538815780E-1,   -9.769762779538815780E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie  avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (1 coeff)  et v (2 coeff) sont correctes, a ne pas inverser


const     arm_iir_lattice_instance_f32    IIR_TX_HPF_90HZ_DECIM =
{
    .numStages = 1,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.953952555907763
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 -0.976976277953882,
		  0.044987260538523
    }
};



// Tx Audio HPF 100 Hz IIR filter  (0.0167) 1er  ordre;  Inverse Tchebechef  1er  ordre// Nizar 12 Avril 2025
// simule par lowa Hills IIR filter designer  vers 6.5:
// HPF 100 Hz ( 0.0167) ,  Inverse Tchebetchef 1er ordre,  fs = 12Khz  (max 1.60 ms)
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcul�s par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -9.488650599446584620E-1 ]
// b = [  9.744325299723293200E-1,  -9.744325299723293200E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie  avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (1 coeff)  et v (2 coeff) sont correctes, � ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_TX_HPF_100HZ_DECIM =
{
    .numStages = 1,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.948865059944658
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 -0.974432529972329,
		  0.049827549008110
    }
};



// Tx Audio HPF 120 Hz IIR filter  (0.02) 1er  ordre;  Inv Tchebechef  1er  ordre// Nizar 12 Avril 2025
// simule par lowa Hills IIR filter designer  vers 6.5:
// HPF 120 Hz ( 0.02) ,  Inverse Tchebetchef 1er ordre,  fs = 12Khz  (max 1.30 ms)
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcules par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -9.390625058174924430E-1  ]
// b = [  9.695312529087461330E-1,  -9.695312529087461330E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie  avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (1 coeff)  et v (2 coeff) sont correctes, � ne pas inverser


const    arm_iir_lattice_instance_f32   IIR_TX_HPF_120HZ_DECIM  =
{
    .numStages = 1,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.939062505817492
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 -0.969531252908746,
		  0.059080805083886
    }
};

// Tx Audio HPF 140 Hz IIR filter  (0.0233) 1er  ordre;  Inv Tchebechef  1er  ordre// Nizar 12 Avril 2025
// simule par lowa Hills IIR filter designer  vers 6.5:
// HPF 140 Hz ( 0.0233) ,  Inverse Tchebetchef 1er ordre,  fs = 12Khz  (max 1.12 ms)
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcules par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -9.293549230213708160E-1 ]
// b = [  9.646774615106854970E-1,  -9.646774615106854970E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie  avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (1 coeff)  et v (2 coeff) sont correctes, a ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_TX_HPF_140HZ_DECIM =
{
    .numStages = 1,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.929354923021371
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 -0.964677461510685,
		  0.068149713527971
    }
};

// Tx Audio HPF 160 Hz IIR filter  (0.0266) 1er  ordre;  Inv Tchebechef  1er  ordre// Nizar 12 Avril 2025
// simule par lowa Hills IIR filter designer  vers 6.5:
// HPF 160 Hz ( 0.0266) ,  Inverse Tchebetchef 1er ordre,  fs = 12Khz  (max 0.990 ms)
// lattice ARMA coefficients designed with MATLAb avec la fonction suivante :
// Lattice (treuillis) coeff calcules par Mathlab online 2020 avec la fonction suivante :
// format long
// a = [  1.000000000000000000,     -9.197404238380585180E-1 ]
// b = [  9.598702119190292590E-1,  -9.598702119190292590E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copie  avec l'ordre inverse de ceux affiches sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB : les correspondances entre k (1 coeff)  et v (2 coeff) sont correctes, � ne pas inverser


const   arm_iir_lattice_instance_f32  IIR_TX_HPF_160HZ_DECIM =
{
    .numStages = 1,
    .pkCoeffs  = (float*) (const float[])
    {
    	 -0.919740423838059
     },

    .pvCoeffs = (float*) (const float[])
    {
    	 -0.959870211919029,
		  0.077038776379094
    }
};


// Designer By Nizar NIZZZZ  le 07/04/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// HPF  85Hz (0,00354) Papouli   3em ordre,   fs = 48K   (5.45 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.000000000000000000, -2.9762893999212241,   2.95284844268572488, -0.97655720456801447   ]
// b = [ 0.988211880896870554, -2.96463564269061131,  2.96463564269061131, -0.9882118808968705540 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (3 coeff)  et v-coeff (4 coeff) ,
/*
#define IIR_TX_HPF_85HZ_numStages 3

const arm_iir_lattice_instance_f32 IIR_TX_HPF_85HZ =
{
    .numStages = IIR_TX_HPF_85HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	-0.976557204568014,
		 0.999904180538820,
    	-0.999960792121653
    },
    .pvCoeffs = (float*) (const float[])
    {
    	-0.988211880896871,
		 0.023431096701040,
		 0.000262381827099,
        -0.000000031038993
    }
};

*/
// Designer By Nizar NIZZZZ  le 26/03/2021 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// HPF  74.4Hz (0.0031) Papouli   4em ordre,   fs = 48K   ( 9.6 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [ 1.000000000000000000, -3.97226133378686974, 5.9171680720214832,  -3.91754903124223786, 0.972642308463816363 ]
// b = [ 0.98622629659465062,  -3.94490518637860221, 5.91735777956790265, -3.94490518637860221, 0.98622629659465062  ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.
// NB :  k-coeff (4 coeff)  et v-coeff (5 coeff) ,
/*
#define IIR_TX_HPF_75HZ_numStages 4

const arm_iir_lattice_instance_f32 IIR_TX_HPF_75HZ =
{
    .numStages = IIR_TX_HPF_75HZ_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.972642308463816,
		-0.999863952634914,
		 0.999972493286781,
       	-0.999971203501974
    },
    .pvCoeffs = (float*) (const float[])
    {
    	 0.986226296594651,
		-0.027356602051850,
		-0.000371978386821,
		 0.000000043690294,
     	 0.000000006392840
    }
};

*/


// Designer By Nizar NIZZZZ  le 15/07/2023 moyennant le logiciel lowa Hills IIR filter designer ver 6.5
// Notch filter 1200Hz bande 65Hz  2 em ordre,   fs = 12K   ( 4.0 ms max )
// calcul� par lowa Hills IIR filter designer ver 6.5  et Mathlab avec la fonction suivante
// format long
// a = [  1.000000000000000000,      -1.585705841802626410E0,    9.600402127865738320E-1 ]
// b = [  9.800201063932869160E-1,   -1.585705841802626410E0,    9.800201063932869160E-1 ]
// [k,v] = tf2latc(b,a)
// NB : Les coeffs doivent etre copi�s avec l'ordre inverse de ceux affich�s sur mathlab ( copier/coller, ajouter les virgules, puis inverser l'ordre des coeffs)
// NB : pas d'inversion de signe des coeffs lors du copiage depuis l'editeur de Mathlab.


/*

const arm_iir_lattice_instance_f32 IIR_TX_Voice_Notch_1200HZ =
{
    .numStages = 2,
    .pkCoeffs  = (float*) (const float[])
    {
    	 0.960040212786574,
    	-0.809016994374946
    },
    .pvCoeffs = (float*) (const float[])
    {
    	0.980020106393287,
	   -0.031682234010760,
      	0.013529929181886
    }
};

*/


//
// 150 - 2850Hz bandpass
//
#define IIR_TX_WIDE_TREBLE_numStages 10
const arm_iir_lattice_instance_f32 IIR_TX_WIDE_TREBLE =
{
    .numStages = IIR_TX_WIDE_TREBLE_numStages,
    .pkCoeffs  = (float*) (const float[])
    {
    	0.71896929903807000000,
		-0.95701929433651100000,
		0.97688821391048500000,
		-0.97914462752169300000,
		0.97381210430161500000,
		-0.98932784460632400000,
		0.99918319918236100000,
		-0.99964609685921500000,
		0.99997374020338600000,
		-0.99980049691966100000


    },

    .pvCoeffs = (float*) (const float[])
    {

    	-0.00032448877846164000,
		-0.00117432406256116000,
		-0.00207458605133000000,
		-0.00219730703764023000,
		-0.00117629105002028000,
		-0.00042269171278772100,
		0.00003803127953775490,
		0.00000159703882026720,
		-0.00000000535014800171,
		-0.00000000013312786485,
		2.67655E-13

    }
};

//
// 250 - 2950Hz bandpass
//
#define IIR_TX_SOPRANO_numStages 10
const arm_iir_lattice_instance_f32 IIR_TX_SOPRANO =
{
    .numStages = IIR_TX_SOPRANO_numStages,
    .pkCoeffs  = (float*) (const float[])
    {

    	0.71987365385562700,
		-0.95478395730601100,
		0.97597041246871300,
		-0.97877331988142200,
		0.97311471309818900,
		-0.98013955262452800,
		0.99791087029646100,
		-0.99903556044544100,
		0.99994405075167400,
		-0.99944176350280300

    },

    .pvCoeffs = (float*) (const float[])
    {

    	-0.00146489605640520,
		-0.00378491009751258,
		-0.00400350501493743,
		-0.00341201203499246,
		-0.00096809938766577,
		-0.00035554457716075,
		0.00007141442667891,
		0.00000426101940838,
		-0.00000003996117780,
		-0.00000000087267111,
		0.00000000000495626

    }
};



/*

// NOTE:
//	- IIR structure is Lattice Autoregressive Moving-Average (ARMA)
//	- ARM FIR/IIR algorithms require time reverse-order coefficients!!!
//
//	10th order Elliptic bandpass filter
//	Fstop:  200, 3250 Hz
//	Fpass:  300, 2575 Hz
//	-6dB points:  285, 2698 Hz
//	-10dB points:  281, 2745 Hz
//	-20dB points:  270, 2845 Hz
//	-30dB points:  <265, >2915 Hz
//
#define IIR_TX_2k7_numStages 10
const arm_iir_lattice_instance_f32 IIR_TX_2k7 = {
  .numStages = IIR_TX_numStages,
  .pkCoeffs  = (float*) (const float[])
{
		0.6630364568624,
		-0.9520893446293,
		0.9760332145655,
		-0.9865766256209,
		0.9831633680402,
		-0.9684840684235,
		0.9968012184072,
		-0.9991378973359,
		0.9999607252996,
		-0.9992727516361
},

  .pvCoeffs = (float*) (const float[])
{
		-0.01765590852027,
		-0.03197343698045,
		-0.009018268963126,
		-0.007790758301124,
		-0.0001813788700895,
		-0.0001640724508686,
		6.107606732009e-05,
		4.622811990096e-06,
		-3.806757115088e-08,
		-5.946272640944e-10,
		4.410607296057e-12
}
};
*/

