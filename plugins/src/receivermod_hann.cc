#include "errorhandling.h"
#include "scene.h"
#include <complex.h>

class rec_hann_t : public TASCAR::receivermod_base_speaker_t {
public:
  class data_t : public TASCAR::receivermod_base_t::data_t {
  public:
    data_t(uint32_t chunksize,uint32_t channels);
    virtual ~data_t();
    // ambisonic weights:
    float* wp;
    float* dwp;
    float* w;
    float* dw;
    float* x;
    float* dx;
    float* y;
    float* dy;
    float* z;
    float* dz;
    double dt;
  };
  rec_hann_t(xmlpp::Element* xmlsrc);
  virtual ~rec_hann_t() {};
  void add_pointsource(const TASCAR::pos_t& prel, double width, const TASCAR::wave_t& chunk, std::vector<TASCAR::wave_t>& output, receivermod_base_t::data_t*);
  receivermod_base_t::data_t* create_data(double srate,uint32_t fragsize);
private:
  double wexp;
};

rec_hann_t::data_t::data_t(uint32_t chunksize,uint32_t channels)
{
  wp = new float[channels];
  dwp = new float[channels];
  w = new float[channels];
  dw = new float[channels];
  x = new float[channels];
  dx = new float[channels];
  y = new float[channels];
  dy = new float[channels];
  z = new float[channels];
  dz = new float[channels];
  for(uint32_t k=0;k<channels;k++)
    wp[k] = dwp[k] = w[k] = dw[k] = x[k] = dx[k] = y[k] = dy[k] = z[k] = dz[k] = 0;
  dt = 1.0/std::max(1.0,(double)chunksize);
}

rec_hann_t::data_t::~data_t()
{
  delete [] wp;
  delete [] dwp;
  delete [] w;
  delete [] dw;
  delete [] x;
  delete [] dx;
  delete [] y;
  delete [] dy;
  delete [] z;
  delete [] dz;
}

rec_hann_t::rec_hann_t(xmlpp::Element* xmlsrc)
  : TASCAR::receivermod_base_speaker_t(xmlsrc),
    //spkpos(xmlsrc),
    wexp(0.5)
{
  GET_ATTRIBUTE(wexp);
}

void rec_hann_t::add_pointsource(const TASCAR::pos_t& prel, double width, const TASCAR::wave_t& chunk, std::vector<TASCAR::wave_t>& output, receivermod_base_t::data_t* sd)
{
  data_t* d((data_t*)sd);
  double az_src(prel.azim());
  uint32_t channels(output.size());
  for(unsigned int k=0;k<channels;k++){
    double az(0.5*channels*fabs(spkpos[k].get_rel_azim(az_src)));
    double w = 0;
    if( az<M_PI ){
      w = 0.5+0.5*cos(az);
      if( wexp != 1.0 )
        w = pow(w,wexp);
    }
    d->dwp[k] = (w - d->wp[k])*d->dt;
  }
  for( unsigned int i=0;i<chunk.size();i++){
    for( unsigned int k=0;k<output.size();k++){
      output[k][i] += (d->wp[k] += d->dwp[k]) * chunk[i];
    }
  }
}

TASCAR::receivermod_base_t::data_t* rec_hann_t::create_data(double srate,uint32_t fragsize)
{
  return new data_t(fragsize,spkpos.size());
}

REGISTER_RECEIVERMOD(rec_hann_t);

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
