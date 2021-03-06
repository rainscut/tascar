#include "irrender.h"
#include <unistd.h>
#include "errorhandling.h"
#include <string.h>

TASCAR::wav_render_t::wav_render_t( const std::string& tscname, const std::string& scene_, bool verbose )
  : tsc_reader_t(tscname,LOAD_FILE,tscname),
    scene(scene_),
    pscene(NULL),
    verbose_(verbose),
    t0(clock()),
    t1(clock()),
    t2(clock())
{
  read_xml();
  if( !pscene )
    throw TASCAR::ErrMsg("Scene "+scene+" not found.");
}

TASCAR::wav_render_t::~wav_render_t()
{
  if( pscene )
    delete pscene;
}

void TASCAR::wav_render_t::add_scene(xmlpp::Element* sne)
{
  if( (!pscene) && (scene.empty() || (sne->get_attribute_value("name") == scene)) ){
    pscene = new render_core_t(sne);
  }
}

void TASCAR::wav_render_t::set_ism_order_range( uint32_t ism_min, uint32_t ism_max )
{
  if( pscene )
    pscene->set_ism_order_range( ism_min, ism_max );
}

void TASCAR::wav_render_t::render(uint32_t fragsize,const std::string& ifname, const std::string& ofname,double starttime, bool b_dynamic)
{
  if( !pscene )
    throw TASCAR::ErrMsg("No scene loaded");
  // open sound files:
  sndfile_handle_t sf_in(ifname);
  chunk_cfg_t cf( sf_in.get_srate(), std::min(fragsize,sf_in.get_frames()), sf_in.get_channels() );
  chunk_cfg_t cffile( cf );
  uint32_t num_fragments((uint32_t)((sf_in.get_frames()-1)/cf.n_fragment)+1);
  // configure maximum delayline length:
  double maxdist((sf_in.get_frames()+1)/cf.f_sample*(pscene->c));
  for(std::vector<TASCAR::Scene::sound_t*>::iterator isnd=pscene->sounds.begin();isnd!=pscene->sounds.end();++isnd){
    (*isnd)->maxdist = maxdist;
  }
  // initialize scene:
  pscene->prepare( cf );
  uint32_t nch_in(pscene->num_input_ports());
  uint32_t nch_out(pscene->num_output_ports());
  sndfile_handle_t sf_out( ofname, cf.f_sample, nch_out);
  // allocate io audio buffer:
  float* sf_in_buf(new float[cffile.n_channels*cffile.n_fragment]);
  float* sf_out_buf(new float[nch_out*cf.n_fragment]);
  // allocate render audio buffer:
  std::vector<float*> a_in;
  for(uint32_t k=0;k<nch_in;++k){
    a_in.push_back(new float[cffile.n_fragment]);
    memset(a_in.back(),0,sizeof(float)*cffile.n_fragment);
  }
  std::vector<float*> a_out;
  for(uint32_t k=0;k<nch_out;++k){
    a_out.push_back(new float[cf.n_fragment]);
    memset(a_out.back(),0,sizeof(float)*cf.n_fragment);
  }
  TASCAR::transport_t tp;
  tp.rolling = true;
  tp.session_time_seconds = starttime;
  tp.session_time_samples = starttime*cf.f_sample;
  tp.object_time_seconds = starttime;
  tp.object_time_samples = starttime*cf.f_sample;
  pscene->process(cf.n_fragment,tp,a_in,a_out);
  if( verbose_ )
    std::cerr << "rendering " << pscene->active_pointsources << " of " << pscene->total_pointsources << " point sources.\n";
  t1 = clock();
  for(uint32_t k=0;k<num_fragments;++k){
    // load audio chunk from file
    uint32_t n_in(sf_in.readf_float(sf_in_buf,cffile.n_fragment));
    if( n_in > 0 ){
      if( n_in < cffile.n_fragment )
        memset(&(sf_in_buf[n_in*cffile.n_channels]),0,(cffile.n_fragment-n_in)*cffile.n_channels*sizeof(float));
      for(uint32_t kf=0;kf<cffile.n_fragment;++kf)
        for(uint32_t kc=0;kc<nch_in;++kc)
          if( kc < cffile.n_channels )
            a_in[kc][kf] = sf_in_buf[kc+cffile.n_channels*kf];
          else
            a_in[kc][kf] = 0.0f;
      // process audio:
      pscene->process(cf.n_fragment,tp,a_in,a_out);
      // save audio:
      for(uint32_t kf=0;kf<cf.n_fragment;++kf)
        for(uint32_t kc=0;kc<nch_out;++kc)
          sf_out_buf[kc+nch_out*kf] = a_out[kc][kf];
      sf_out.writef_float(sf_out_buf,cf.n_fragment);
      // increment time:
      if( b_dynamic ){
        tp.session_time_samples += cf.n_fragment;
        tp.session_time_seconds = ((double)tp.session_time_samples)/cf.f_sample;
        tp.object_time_samples += cf.n_fragment;
        tp.object_time_seconds = ((double)tp.object_time_samples)/cf.f_sample;
      }
    }
  }
  t2 = clock();
  pscene->release();
  // de-allocate render audio buffer:
  for(uint32_t k=0;k<nch_in;++k)
    delete [] a_in[k];
  for(uint32_t k=0;k<nch_out;++k)
    delete [] a_out[k];
  delete [] sf_in_buf;
  delete [] sf_out_buf;
}


void TASCAR::wav_render_t::render_ir(uint32_t len, double fs, const std::string& ofname,double starttime)
{
  if( !pscene )
    throw TASCAR::ErrMsg("No scene loaded");
  // configure maximum delayline length:
  double maxdist((len+1)/fs*(pscene->c));
  //std::vector<TASCAR::Scene::sound_t*> snds(pscene->linearize_sounds());
  for(std::vector<TASCAR::Scene::sound_t*>::iterator isnd=pscene->sounds.begin();isnd!=pscene->sounds.end();++isnd){
    (*isnd)->maxdist = maxdist;
  }
  chunk_cfg_t cf( fs, len );
  // initialize scene:
  pscene->prepare( cf );
  uint32_t nch_in(pscene->num_input_ports());
  uint32_t nch_out(pscene->num_output_ports());
  sndfile_handle_t sf_out(ofname,fs,nch_out);
  // allocate io audio buffer:
  float sf_out_buf[nch_out*len];
  // allocate render audio buffer:
  std::vector<float*> a_in;
  for(uint32_t k=0;k<nch_in;++k){
    a_in.push_back(new float[len]);
    memset(a_in.back(),0,sizeof(float)*len);
  }
  std::vector<float*> a_out;
  for(uint32_t k=0;k<nch_out;++k){
    a_out.push_back(new float[len]);
    memset(a_out.back(),0,sizeof(float)*len);
  }
  TASCAR::transport_t tp;
  tp.rolling = false;
  tp.session_time_seconds = starttime;
  tp.session_time_samples = starttime*fs;
  tp.object_time_seconds = starttime;
  tp.object_time_samples = starttime*fs;
  pscene->process(len,tp,a_in,a_out);
  if( verbose_ )
    std::cerr << "rendering " << pscene->active_pointsources << " of " << pscene->total_pointsources << " point sources.\n";
  a_in[0][0] = 1.0f;
  // process audio:
  pscene->process(len,tp,a_in,a_out);
  // save audio:
  for(uint32_t kf=0;kf<len;++kf)
    for(uint32_t kc=0;kc<nch_out;++kc)
      sf_out_buf[kc+nch_out*kf] = a_out[kc][kf];
  sf_out.writef_float(sf_out_buf,len);
  // increment time:
  // de-allocate render audio buffer:
  pscene->release();
  for(uint32_t k=0;k<nch_in;++k)
    delete [] a_in[k];
  for(uint32_t k=0;k<nch_out;++k)
    delete [] a_out[k];
}

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
