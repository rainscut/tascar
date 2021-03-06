#include "speakerarray.h"
#include "errorhandling.h"
#include <complex.h>
#include <algorithm>
#include <random>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <chrono>
#include <string.h>

using namespace TASCAR;

spk_array_cfg_t::spk_array_cfg_t( xmlpp::Element* xmlsrc, bool use_parent_xml )
  : xml_element_t( xmlsrc ),
    e_layout(NULL)
{
  if( use_parent_xml ){
    e_layout = xmlsrc;
  }else{
    GET_ATTRIBUTE(layout);
    if( layout.empty() )
      throw TASCAR::ErrMsg("No speaker layout file provided.");
    try{
      domp.parse_file( TASCAR::env_expand( layout ) );
    }
    catch( const xmlpp::internal_error& e){
      throw TASCAR::ErrMsg(std::string("xml internal error: ")+e.what());
    }
    catch( const xmlpp::validity_error& e){
      throw TASCAR::ErrMsg(std::string("xml validity error: ")+e.what());
    }
    catch( const xmlpp::parse_error& e){
      throw TASCAR::ErrMsg(std::string("xml parse error: ")+e.what());
    }
    if( !domp )
      throw TASCAR::ErrMsg("Unable to parse file \""+layout+"\".");
    e_layout = domp.get_document()->get_root_node();
    if( !e_layout )
      throw TASCAR::ErrMsg("No root node found in document \""+layout+"\".");
    if( e_layout->get_name() != "layout" )
      throw TASCAR::ErrMsg("Invalid root node name. Expected \"layout\", got "+e_layout->get_name()+".");
  }
}

// speaker array:
spk_array_t::spk_array_t(xmlpp::Element* e, 
                         bool use_parent_xml,
                         const std::string& elementname_)
  : spk_array_cfg_t( e, use_parent_xml ),
    elayout( e_layout ),
    rmax(0),
    rmin(0),
    xyzgain(1.0),
    elementname(elementname_),
    mean_rotation(0)
{
  clear();
  xmlpp::Node::NodeList subnodes(e_layout->get_children());
  for( auto sn=subnodes.begin(); sn!=subnodes.end(); ++sn ){
    xmlpp::Element* sne(dynamic_cast<xmlpp::Element*>(*sn));
    if( sne && ( sne->get_name() == elementname )){
      emplace_back(sne);
    }
  }
  elayout.GET_ATTRIBUTE(xyzgain);
  // 
  if( empty() )
    throw TASCAR::ErrMsg("Invalid " + 
                         elementname_ + 
                         " array (no "+
                         elementname_ +
                         " elements defined).");
  // update derived parameters:
  rmax = operator[](0).norm();
  rmin = rmax;
  for(uint32_t k=1;k<size();k++){
    double r(operator[](k).norm());
    if( rmax < r )
      rmax = r;
    if( rmin > r )
      rmin = r;
  }
  double _Complex p_xy(0);
  for(uint32_t k=0;k<size();k++){
    operator[](k).spkgain *= operator[](k).norm()/rmax;
    operator[](k).dr = rmax-operator[](k).norm();
    p_xy += cexp( -(double)k*PI2*I/(double)(size()))*(operator[](k).unitvector.x+I*operator[](k).unitvector.y);
  }
  mean_rotation = carg(p_xy);
  didx.resize(size());
  for(uint32_t k=0;k<size();++k){
    operator[](k).update_foa_decoder(1.0f/size(), xyzgain);
    connections.push_back(operator[](k).connect);
    float w(1);
    for(uint32_t l=0;l<size();++l){
      if( l != k ){
        float d(dot_prod(operator[](k).unitvector,operator[](l).unitvector));
        if( d > 0 )
          w+= d;
      }
    }
    operator[](k).densityweight = size()/w;
  }
  float dwmean(0);
  for(uint32_t k=0;k<size();++k)
    dwmean += operator[](k).densityweight;
  dwmean /= size();
  for(uint32_t k=0;k<size();++k)
    operator[](k).densityweight /= dwmean;
}

double spk_descriptor_t::get_rel_azim(double az_src) const
{
  return carg(cexp( I *(az_src - az)));
}

double spk_descriptor_t::get_cos_adist(pos_t src_unit) const
{
  return dot_prod( src_unit, unitvector );
}

bool sort_didx(const spk_array_t::didx_t& a,const spk_array_t::didx_t& b)
{
  return (a.d > b.d);
}

const std::vector<spk_array_t::didx_t>& spk_array_t::sort_distance(const pos_t& psrc)
{
  for(uint32_t k=0;k<size();++k){
    didx[k].idx = k;
    didx[k].d = dot_prod(psrc,operator[](k).unitvector);
  }
  std::sort(didx.begin(),didx.end(),sort_didx);
  return didx;
}

void spk_array_diff_render_t::render_diffuse( std::vector<TASCAR::wave_t>& output )
{
  uint32_t channels(size());
  if( output.size() != channels )
    throw TASCAR::ErrMsg("Invalid size of speaker array");
  if( !diffuse_field_accumulator )
    throw TASCAR::ErrMsg("No diffuse field accumulator allocated.");
  if( !diffuse_render_buffer )
    throw TASCAR::ErrMsg("No diffuse field render buffer allocated.");
  uint32_t N(diffuse_field_accumulator->size());
  for( uint32_t ch=0; ch<channels; ++ch ){
    for( uint32_t t=0; t<N; ++t ){
      (*diffuse_render_buffer)[t] =
        operator[](ch).d_w * diffuse_field_accumulator->w()[t];
      (*diffuse_render_buffer)[t] +=
        operator[](ch).d_x * diffuse_field_accumulator->x()[t];
      (*diffuse_render_buffer)[t] +=
        operator[](ch).d_y * diffuse_field_accumulator->y()[t];
      (*diffuse_render_buffer)[t] +=
        operator[](ch).d_z * diffuse_field_accumulator->z()[t];
    }
    if( densitycorr )
      *diffuse_render_buffer *= operator[](ch).densityweight;
    if( decorr )
      decorrflt[ch].process( *diffuse_render_buffer, output[ch], true );
    else
      output[ch] += *diffuse_render_buffer;
  }
  diffuse_field_accumulator->clear();
}

spk_descriptor_t::spk_descriptor_t(xmlpp::Element* xmlsrc)
  : xml_element_t(xmlsrc),
    az(0.0),
    el(0.0),
    r(1.0),
    delay(0.0),
    gain(1.0),
    spkgain(1.0),
    dr(0.0),      
    d_w(0.0f),
    d_x(0.0f),
    d_y(0.0f),
    d_z(0.0f),
    densityweight(1.0),
    comp(NULL)
{
  GET_ATTRIBUTE_DEG(az);
  GET_ATTRIBUTE_DEG(el);
  GET_ATTRIBUTE(r);
  GET_ATTRIBUTE(delay);
  GET_ATTRIBUTE(label);
  GET_ATTRIBUTE(connect);
  GET_ATTRIBUTE(compB);
  GET_ATTRIBUTE_DB(gain);
  set_sphere(r,az,el);
  unitvector = normal();
  update_foa_decoder(1.0f, 1.0 );
}

spk_descriptor_t::spk_descriptor_t(const spk_descriptor_t& src)
  : xml_element_t(src),
    pos_t(src),
    az(src.az),
    el(src.el),
    r(src.r),
    delay(src.delay),
    label(src.label),
    connect(src.connect),
    compB(src.compB),
    gain(src.gain),
    unitvector(src.unitvector),
    spkgain(src.spkgain),
    dr(src.dr),      
    d_w(src.d_w),
    d_x(src.d_x),
    d_y(src.d_y),
    d_z(src.d_z),
    densityweight(src.densityweight),
    comp(NULL)
{
}

spk_descriptor_t::~spk_descriptor_t()
{
  if( comp )
    delete comp;
}

void spk_descriptor_t::update_foa_decoder(float gain, double xyzgain)
{
  // update of FOA decoder matrix:
  d_w = 1.4142135623730951455f * gain;
  gain *= xyzgain;
  d_x = unitvector.x * gain;
  d_y = unitvector.y * gain;
  d_z = unitvector.z * gain;
}

void spk_array_t::prepare( chunk_cfg_t& cf_ )
{
  audiostates_t::prepare( cf_ );
  delaycomp.clear();
  for(uint32_t k=0;k<size();++k)
    delaycomp.emplace_back( f_sample*((operator[](k).dr/340.0)+operator[](k).delay) );
  // speaker correction filter:
  for( uint32_t k=0;k<size();++k ){
    spk_descriptor_t& spk(operator[](k));
    if(  spk.compB.size() > 0 ){
      spk.comp = new TASCAR::overlap_save_t( n_fragment+1, n_fragment );
      spk.comp->set_irs( spk.compB, false );
    }
  }
}

void spk_array_diff_render_t::prepare( chunk_cfg_t& cf_ )
{
  spk_array_t::prepare( cf_ );
  // initialize decorrelation filter:
  decorrflt.clear();
  uint32_t irslen(decorr_length*f_sample);
  uint32_t paddedirslen((1<<(int)(ceil(log2(irslen+n_fragment-1))))-n_fragment+1);
  for(uint32_t k=0;k<size();++k)
    decorrflt.push_back(TASCAR::overlap_save_t(paddedirslen,n_fragment));
  TASCAR::fft_t fft_filter(irslen);
  std::mt19937 gen(1);
  std::uniform_real_distribution<double> dis(0.0, 2*M_PI);
  //std::exponential_distribution<double> dis(1.0);
  for(uint32_t k=0;k<size();++k){
    for(uint32_t b=0;b<fft_filter.s.n_;++b)
      fft_filter.s[b] = cexp(I*dis(gen));
    fft_filter.ifft();
    for(uint32_t t=0;t<fft_filter.w.n;++t)
      fft_filter.w[t] *= (0.5-0.5*cos(t*PI2/fft_filter.w.n));
    decorrflt[k].set_irs(fft_filter.w,false);
  }
  // end of decorrelation filter.
  if( diffuse_field_accumulator )
    delete diffuse_field_accumulator;
  diffuse_field_accumulator = NULL;
  diffuse_field_accumulator = new TASCAR::amb1wave_t(n_fragment);
  if( diffuse_render_buffer )
    delete diffuse_render_buffer;
  diffuse_render_buffer = NULL;
  diffuse_render_buffer = new TASCAR::wave_t(n_fragment);
}


void spk_array_diff_render_t::add_diffuse_sound_field( const TASCAR::amb1wave_t& diff )
{
  if( !diffuse_field_accumulator )
    throw TASCAR::ErrMsg("No diffuse field accumulator allocated.");
  *diffuse_field_accumulator += diff;
}

spk_array_diff_render_t::~spk_array_diff_render_t()
{
  if( diffuse_field_accumulator )
    delete diffuse_field_accumulator;
  if( diffuse_render_buffer )
    delete diffuse_render_buffer;
}

spk_array_diff_render_t::spk_array_diff_render_t(xmlpp::Element* e, 
                                                 bool use_parent_xml,
                                                 const std::string& elementname_)
  : spk_array_t( e, use_parent_xml, elementname_ ),
    diffuse_field_accumulator(NULL),
    diffuse_render_buffer(NULL),
    decorr_length(0.05),
    decorr(true),
    densitycorr(true),
    caliblevel(50000),
    diffusegain(1.0),
    calibage(0)
{
  elayout.GET_ATTRIBUTE(decorr_length);
  elayout.GET_ATTRIBUTE_BOOL(decorr);
  elayout.GET_ATTRIBUTE_BOOL(densitycorr);
  has_caliblevel = elayout.has_attribute("caliblevel");
  elayout.GET_ATTRIBUTE_DB(caliblevel);
  has_diffusegain = elayout.has_attribute("diffusegain");
  elayout.GET_ATTRIBUTE_DB(diffusegain);
  has_calibdate = elayout.has_attribute("calibdate");
  elayout.GET_ATTRIBUTE(calibdate);
  if( !calibdate.empty() ){
    std::time_t now(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    std::tm tcalib;
    memset(&tcalib,0,sizeof(tcalib));
    const char* msg(strptime(calibdate.c_str(),"%Y-%m-%d %H:%M:%S",&tcalib));
    if( !msg )
      msg = strptime(calibdate.c_str(),"%Y-%m-%d",&tcalib);
    if( msg ){
      std::time_t ctcalib(mktime(&tcalib));
      calibage = difftime(now,ctcalib)/(24*3600);
    }else{
      TASCAR::add_warning("Invalid date/time format: "+calibdate);
    }
  }
}


/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
