/*---------------------------------------------------------------------------*\

  FILE........: tcohpsk.c
  AUTHORS.....: David Rowe & Steve Sampson
  DATE CREATED: June 2017

  Tests for the C version of the OFDM modem.  This program
  outputs a file of Octave vectors that are loaded and automatically
  tested against the Octave version of the modem by the Octave script
  tofdm.m

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2017 David Rowe

  All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License version 2, as
  published by the Free Software Foundation.  This program is
  distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#include "ofdm_internal.h"
#include "codec2_ofdm.h"
#include "octave.h"
#include "test_bits_ofdm.h"

#define NFRAMES 3

int main(int argc, char *argv[])
{
    int            samples_per_frame = ofdm_get_samples_per_frame();
    int            max_samples_per_frame = ofdm_get_max_samples_per_frame();

    struct OFDM   *ofdm;
    COMP           tx[samples_per_frame];      /* one frame of tx samples */
    int            rx_bits[OFDM_BITSPERFRAME];    /* one frame of rx bits    */

    /* log arrays */

    int            tx_bits_log[OFDM_BITSPERFRAME*NFRAMES];
    COMP           tx_log[samples_per_frame*NFRAMES];
    COMP           rxbuf_in_log[max_samples_per_frame*NFRAMES];
    COMP           rxbuf_log[OFDM_RXBUF*NFRAMES];
    COMP           rx_sym_log[(OFDM_NS + 3)*NFRAMES][OFDM_NC + 2];
    float          foff_hz_log[NFRAMES];
    int            rx_bits_log[OFDM_BITSPERFRAME*NFRAMES];

    FILE          *fout;
    int            f,i,j;

    ofdm = ofdm_create(); assert(ofdm != NULL);

    /* Main Loop ---------------------------------------------------------------------*/

    for(f=0; f<NFRAMES; f++) {

	/* --------------------------------------------------------*\
	                          Mod
	\*---------------------------------------------------------*/

        /* todo: add a longer sequence of test bits through
           ofdm_get/put test bits functin similat to cohpsk/fdmdv */

        ofdm_mod(ofdm, (COMP*)tx, test_bits_ofdm);

        /* tx vector logging */

	memcpy(&tx_bits_log[OFDM_BITSPERFRAME*f], test_bits_ofdm, sizeof(int)*OFDM_BITSPERFRAME);
	memcpy(&tx_log[samples_per_frame*f], tx, sizeof(COMP)*samples_per_frame);
    }

    /* --------------------------------------------------------*\
	                        Demod
    \*---------------------------------------------------------*/

    COMP *rx = tx_log;

    /* Init rx with ideal timing so we can test with timing estimation disabled */

    int  Nsam = samples_per_frame*NFRAMES;
    int  prx = 0;
    int  nin = samples_per_frame + 2*(OFDM_M+OFDM_NCP);

    int  lnew;
    COMP rxbuf_in[max_samples_per_frame];

    for (i=0; i<nin; i++,prx++) {
         ofdm->rxbuf[OFDM_RXBUF-nin+i] = rx[prx].real + I*rx[prx].imag;
    }

    int nin_tot = 0;

    /* disable estimators for initial testing */

    ofdm_set_verbose(ofdm, true);
    ofdm_set_timing_enable(ofdm, false);
    ofdm_set_foff_est_enable(ofdm, true);
    ofdm_set_phase_est_enable(ofdm, false);

    for(f=0; f<NFRAMES; f++) {
        /* For initial testng, timing est is off, so nin is always
           fixed.  TODO: we need a constant for rxbuf_in[] size that
           is the maximum possible nin */

        nin = ofdm_get_nin(ofdm);
        assert(nin <= max_samples_per_frame);

        /* Insert samples at end of buffer, set to zero if no samples
           available to disable phase estimation on future pilots on
           last frame of simulation. */

        if ((Nsam-prx) < nin) {
            lnew = Nsam-prx;
        } else {
            lnew = nin;
        }
        //printf("nin: %d prx: %d lnew: %d\n", nin, prx, lnew);
        for(i=0; i<nin; i++) {
            rxbuf_in[i].real = 0.0;
            rxbuf_in[i].imag = 0.0;
        }

        if (lnew) {
            for(i=0; i<lnew; i++, prx++) {
                rxbuf_in[i] = rx[prx];
            }
        }
        assert(prx <= max_samples_per_frame*NFRAMES);

        ofdm_demod(ofdm, rx_bits, rxbuf_in);

        /* rx vector logging -----------------------------------*/

        assert(nin_tot < samples_per_frame*NFRAMES);
	memcpy(&rxbuf_in_log[nin_tot], rxbuf_in, sizeof(COMP)*nin);
        nin_tot += nin;

        for(i=0; i<OFDM_RXBUF; i++) {
            rxbuf_log[OFDM_RXBUF*f+i].real = crealf(ofdm->rxbuf[i]);
            rxbuf_log[OFDM_RXBUF*f+i].imag = cimagf(ofdm->rxbuf[i]);
       }

        for (i = 0; i < (OFDM_NS + 3); i++) {
            for (j = 0; j < (OFDM_NC + 2); j++) {
                rx_sym_log[(OFDM_NS + 3)*f+i][j].real = crealf(ofdm->rx_sym[i][j]);
                rx_sym_log[(OFDM_NS + 3)*f+i][j].imag = cimagf(ofdm->rx_sym[i][j]);
            }
        }

        foff_hz_log[f] = ofdm->foff_est_hz;

        memcpy(&rx_bits_log[OFDM_BITSPERFRAME*f], rx_bits, sizeof(rx_bits));
    }

    /*---------------------------------------------------------*\
               Dump logs to Octave file for evaluation
                      by tofdm.m Octave script
    \*---------------------------------------------------------*/

    fout = fopen("tofdm_out.txt","wt");
    assert(fout != NULL);
    fprintf(fout, "# Created by tofdm.c\n");
    octave_save_complex(fout, "W_c", (COMP*)ofdm->W, OFDM_NC + 2, OFDM_M, OFDM_M);
    octave_save_int(fout, "tx_bits_log_c", tx_bits_log, 1, OFDM_BITSPERFRAME*NFRAMES);
    octave_save_complex(fout, "tx_log_c", (COMP*)tx_log, 1, samples_per_frame*NFRAMES,  samples_per_frame*NFRAMES);
    octave_save_complex(fout, "rxbuf_in_log_c", (COMP*)rxbuf_in_log, 1, nin_tot, nin_tot);
    octave_save_complex(fout, "rxbuf_log_c", (COMP*)rxbuf_log, 1, OFDM_RXBUF*NFRAMES,  OFDM_RXBUF*NFRAMES);
    octave_save_complex(fout, "rx_sym_log_c", (COMP*)rx_sym_log, (OFDM_NS + 3)*NFRAMES, OFDM_NC + 2, OFDM_NC + 2);
    octave_save_float(fout, "foff_hz_log_c", foff_hz_log, NFRAMES, 1, 1);
    octave_save_int(fout, "rx_bits_log_c", rx_bits_log, 1, OFDM_BITSPERFRAME*NFRAMES);
    fclose(fout);

    ofdm_destroy(ofdm);

    return 0;
}

