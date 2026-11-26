#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""known_issues.py - documented defects accepted by the N96K host tests.

The host tests are a regression net, not a bug tracker: a table inconsistency
that is already understood and is not owned by the CI task must not block the
build.  Such findings are reported as KNOWN (XFAIL) and listed here with the
reason.  Every entry is keyed (check_name, symbol_key):

    check_name  - Results.name of the check module (e.g. "filters")
    symbol_key  - stable "<file>:<symbol>" identifier printed by the check

When a defect is fixed the entry becomes stale and run_all.py prints a warning
asking for the entry to be removed (intentional: it keeps the list honest).

Owner: P2 CI/host tests task.  These entries are audit findings, NOT rollbacks
of Nizar functionality - the tables themselves are left untouched.
"""

KNOWN_ISSUES = {
    # check     key                                reason
    ("filters", "iir_3k.c:IIR_3k"): (
        "audit finding (P2 host tests): numStages=10 but pkCoeffs has 11 "
        "elements.  IIR_3k is dead code (declared in filters.h, no other "
        "reference in the tree) so the wrong table is not used by the DSP "
        "path.  Not fixed here: filter drivers are outside the CI task scope."
    ),
    ("audio_instances", "audio_driver.c:IIR_biquad_Zoom_FFT_I"): (
        "placeholder initializer: numStages=4 but only 2 cascaded biquad "
        "sections (10 pCoeffs instead of 20).  pCoeffs is replaced at runtime "
        "in AudioDriver_SetRxAudioProcessing() before the filter runs, so this "
        "is a latent UB / OOB-read risk until the first assignment, not a "
        "functional bug today.  Not fixed here: audio_driver.c is owned by the "
        "audio patch."
    ),
    ("audio_instances", "audio_driver.c:IIR_biquad_Zoom_FFT_Q"): (
        "same placeholder pattern as IIR_biquad_Zoom_FFT_I (numStages=4, "
        "10 pCoeffs).  pCoeffs is replaced at runtime before use."
    ),
}
