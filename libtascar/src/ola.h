/* License (GPL)
 *
 * Copyright (C) 2018  Giso Grimm
 *
 * This program is free software; you can redistribute it and/ or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#ifndef OLA_H
#define OLA_H

#include "stft.h"

namespace TASCAR {

  /**
     \brief Implementation of the overlap-add method
   */
  class ola_t : public stft_t {
  public:
    /**
       \brief Constructor
       \param fftlen FFT length
       \param wndlen Length of analysis window
       \param chunksize Length of signal chunk (= window shift)
       \param wnd Analysis window type
       \param zerownd Synthesis window type of zero padded parts
       \param wndpos Position of analysis window
       - 0 = zero padding only at end
       - 0.5 = symmetric zero padding
       - 1 = zero padding only at beginning

       \param postwnd Synthesis window type of whole chunk, including analysis window and zero padded parts.
     */
    ola_t(uint32_t fftlen, uint32_t wndlen, uint32_t chunksize, windowtype_t wnd, windowtype_t zerownd,double wndpos,windowtype_t postwnd=WND_RECT);
    void ifft(wave_t& wOut);
  private:
    wave_t zwnd1;
    wave_t zwnd2;
    wave_t pwnd;
    bool apply_pwnd;
    wave_t long_out;
  };

  /**
     \brief Implementation of overlap-save-method (FFT-based FIR
     filter)

     The overlap-save method is realized as a special case of the
     overlap-add method.
   */
  class overlap_save_t : public ola_t {
  public:
    /**
       \brief Constructor

       The FFT length is irslen+chunksize-1.
       
       \param irslen Maximal impulse response length.
       \param chunksize Size of audio chunks in process call.
     */
    overlap_save_t(uint32_t irslen,uint32_t chunksize);
    
    /**
       \brief Set filter coefficients.

       \note This method is not real-time safe.
       \note This method is not thread safe.

       \param h Filter coefficients
       \param check Test if length matches the IRS length provided in constructor. Set to false to allow for shorter impulse responses.
     */
    void set_irs(const TASCAR::wave_t& h,bool check=true);
    
    /**
       \brief Set filter transfer function.

       \note This method is not real-time safe.
       \note This method is not thread safe.

       \param H Filter transfer function. The frequency resolution
       must match the FFT length of the filter.
     */
    void set_spec(const TASCAR::spec_t& H);
    
    /**
       \brief Filter one chunk of audio data.
       \param inchunk Input audio signal
       \retval outchunk Reference to output audio signal container
       \param add Add to buffer (true) or replace buffer (false)
     */
    void process(const TASCAR::wave_t& inchunk,TASCAR::wave_t& outchunk,bool add=true);
  private:
    uint32_t irslen_;
    TASCAR::spec_t H_long;
    TASCAR::wave_t out;
  };

}

#endif
/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
