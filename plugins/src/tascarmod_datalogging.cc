#include "session.h"
#include <fstream>
#include "datalogging_glade.h"
#include <gtkmm.h>
#include <gtkmm/builder.h>
#include <gtkmm/window.h>
#include <matio.h>
#include <lsl_c.h>
#include <sys/types.h>
#include <sys/stat.h>

class recorder_t;

void error_message(const std::string& msg)
{
  std::cerr << "Error: " << msg << std::endl;
  Gtk::MessageDialog dialog("Error",false,Gtk::MESSAGE_ERROR);
  dialog.set_secondary_text(msg);
  dialog.run();
}

std::string datestr()
{
  time_t t(time(NULL));
  struct tm* ltm = localtime(&t);
  char ctmp[1024];
  strftime(ctmp,1024,"%Y%m%d_%H%M%S",ltm);
  return ctmp;
}

class lslvar_t : public TASCAR::xml_element_t {
public:
  lslvar_t(xmlpp::Element* xmlsrc, double lsltimeout);
  ~lslvar_t();
  uint32_t size;
  std::string name;
  recorder_t* recorder;
  void set_delta(double deltatime);
  void poll_data();
  void get_stream_delta_start();
  void get_stream_delta_end();
private:
  double get_stream_delta();
  std::string predicate;
public:
  lsl_streaminfo info;
private:
  lsl_inlet inlet;
  double delta;
public:
  double stream_delta_start;
  double stream_delta_end;
  bool time_correction_failed;
  double tctimeout;
};

lslvar_t::lslvar_t(xmlpp::Element* xmlsrc, double lsltimeout)
  : xml_element_t(xmlsrc),
    size(0),
    recorder(NULL),
    delta(lsl_local_clock()),
    stream_delta_start(0),
    stream_delta_end(0),
    time_correction_failed(false),
    tctimeout(2.0)
{
  GET_ATTRIBUTE(predicate);
  GET_ATTRIBUTE(tctimeout);
  if( predicate.empty() )
    throw TASCAR::ErrMsg("Invalid (empty) predicate.");
  char c_predicate[predicate.size()+1];
  //std::vector<lsl::stream_info> infos(lsl::resolve_stream(predicate,1,lsltimeout));
  memmove(c_predicate,predicate.c_str(),predicate.size()+1);
  int ec(lsl_resolve_bypred(&info,1, c_predicate, 1, lsltimeout ));
  //int ec(lsl_resolve_byprop(&info,1, "name", c_predicate, 1, lsltimeout ));
  std::cerr << "resolved LSL predicate " << predicate << std::endl;
  if( ec <= 0 ){
    switch( ec ){
    case lsl_timeout_error : throw TASCAR::ErrMsg("LSL timeout error ("+predicate+")."); break;
    case lsl_lost_error : throw TASCAR::ErrMsg("LSL lost stream error ("+predicate+")."); break;
    case lsl_argument_error : throw TASCAR::ErrMsg("LSL argument error ("+predicate+")."); break;
    case lsl_internal_error : throw TASCAR::ErrMsg("LSL internal error ("+predicate+")."); break;
    case 0 : throw TASCAR::ErrMsg("No LSL stream found ("+predicate+")."); break;
    }
    throw TASCAR::ErrMsg("Unknown LSL error ("+predicate+").");
  }
  inlet = lsl_create_inlet(info, 300, LSL_NO_PREFERENCE, 1);
  std::cerr << "created LSL inlet for predicate " << predicate << std::endl;
  size = lsl_get_channel_count(info)+1;
  name = lsl_get_name(info);
  std::cerr << "measuring LSL time correction: ";
  get_stream_delta_start();
  std::cerr << stream_delta_start << " s\n";
}

void lslvar_t::get_stream_delta_start()
{
  stream_delta_start = get_stream_delta();
}

void lslvar_t::get_stream_delta_end()
{
  stream_delta_end = get_stream_delta();
}

double lslvar_t::get_stream_delta()
{
  int ec(0);
  double stream_delta(lsl_time_correction( inlet, tctimeout, &ec ));
  if( ec != 0 ){
    try{
      switch( ec ){
      case lsl_timeout_error : throw TASCAR::ErrMsg("LSL timeout error ("+predicate+")."); break;
      case lsl_lost_error : throw TASCAR::ErrMsg("LSL lost stream error ("+predicate+")."); break;
      }
      throw TASCAR::ErrMsg("Unknown LSL error ("+predicate+").");
    }
    catch( const std::exception& e){
      std::cerr << "Warning: " << e.what() << std::endl;
      stream_delta = 0;
      time_correction_failed = true;
    }
  }
  return stream_delta;
}
  
lslvar_t::~lslvar_t()
{
  lsl_destroy_inlet(inlet);
}

void lslvar_t::set_delta(double deltatime)
{
  delta = deltatime;
}

class oscvar_t : public TASCAR::xml_element_t {
public:
  oscvar_t(xmlpp::Element* xmlsrc);
  std::string get_fmt();
  std::string path;
  uint32_t size;
  bool ignorefirst;
};

oscvar_t::oscvar_t(xmlpp::Element* xmlsrc)
  : xml_element_t(xmlsrc),
    ignorefirst(false)
{
  GET_ATTRIBUTE(path);
  GET_ATTRIBUTE(size);
  GET_ATTRIBUTE_BOOL(ignorefirst);
}

std::string oscvar_t::get_fmt()
{
  return std::string(size,'f');
}

typedef std::vector<oscvar_t> oscvarlist_t;
typedef std::vector<lslvar_t*> lslvarlist_t;

class dlog_vars_t  : public TASCAR::module_base_t {
public:
  dlog_vars_t( const TASCAR::module_cfg_t& cfg );
  ~dlog_vars_t();
  virtual void update(uint32_t frame,bool running);
  void set_jack_client(jack_client_t* jc) { jc_ = jc;};
protected:
  std::string multicast;
  std::string port;
  std::string fileformat;
  std::string outputdir;
  bool displaydc;
  double lsltimeout;
  oscvarlist_t vars;
  lslvarlist_t lslvars;
  jack_client_t* jc_;
};

dlog_vars_t::dlog_vars_t( const TASCAR::module_cfg_t& cfg )
  : module_base_t( cfg ),
    fileformat("matcell"),
    displaydc(true),
    lsltimeout(10.0),
    jc_(NULL)
{
  GET_ATTRIBUTE(multicast);
  GET_ATTRIBUTE(port);
  GET_ATTRIBUTE(fileformat);
  GET_ATTRIBUTE(outputdir);
  GET_ATTRIBUTE(lsltimeout);
  GET_ATTRIBUTE_BOOL(displaydc);
  if( fileformat.size() == 0 )
    fileformat = "matcell";
  if( (fileformat != "txt") && (fileformat != "mat") && (fileformat != "matcell") )
    throw TASCAR::ErrMsg("Invalid file format \""+fileformat+"\".");
  xmlpp::Node::NodeList subnodes = e->get_children();
  for(xmlpp::Node::NodeList::iterator sn=subnodes.begin();sn!=subnodes.end();++sn){
    xmlpp::Element* sne(dynamic_cast<xmlpp::Element*>(*sn));
    if( sne && ( sne->get_name() == "variable" ))
      vars.push_back(oscvar_t(sne));
    if( sne && ( sne->get_name() == "osc" ))
      vars.push_back(oscvar_t(sne));
    if( sne && ( sne->get_name() == "lsl" ))
      lslvars.push_back(new lslvar_t(sne,lsltimeout));
  }
}

void dlog_vars_t::update(uint32_t frame,bool running)
{
  if( jc_ ){
    double delta(jack_get_current_transport_frame(jc_)*t_sample);
    delta -= lsl_local_clock();
    delta *= -1;
    for(lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it)
      (*it)->set_delta(delta);
  }
}

dlog_vars_t::~dlog_vars_t()
{
  for(lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it)
    delete *it;
}

std::string nice_name(std::string s,std::string extend="")
{
  for( uint32_t k=0;k<s.size();k++)
    switch( s[k] ){
    case '/':
    case ':':
    case '.':
    case ' ':
    case '-':
    case '+':
      s[k] = '_';
    }
  while( s.size() && (s[0]=='_') )
    s.erase(0,1);
  if( extend.size() )
    return s+"."+extend;
  return s;
}

class recorder_t : public Gtk::DrawingArea {
public:
  recorder_t(uint32_t size,const std::string& name,pthread_mutex_t& mtx,jack_client_t* jc,double srate,bool ignore_first=false);
  virtual ~recorder_t();
  void store(uint32_t n,double* data);
  const std::vector<double>& get_data() const { return data_;};
  uint32_t get_size() const { return size_;};
  const std::string& get_name() const { return name_; };
  static int osc_setvar(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
  void osc_setvar(const char *path, uint32_t size, double* data);
  void clear();
  virtual bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_timeout();
  void set_displaydc( bool displaydc ){ displaydc_ = displaydc; };
private:
  void get_valuerange( const std::vector<double>& data, uint32_t channels, uint32_t firstchannel, uint32_t n1, uint32_t n2, double& dmin, double& dmax);
  bool displaydc_;
  uint32_t size_;
  std::vector<double> data_;
  std::vector<double> plotdata_;
  std::vector<double> vdc;
  std::string name_;
  pthread_mutex_t& mtx_;
  jack_client_t* jc_;
  double israte_;
  pthread_mutex_t drawlock;
  pthread_mutex_t plotdatalock;
  sigc::connection connection_timeout;
  bool ignore_first_;
};

recorder_t::recorder_t(uint32_t size,const std::string& name,pthread_mutex_t& mtx,jack_client_t* jc,double srate,bool ignore_first)
  : displaydc_(true),
    size_(size),
    vdc(size_,0),
    name_(name),
    mtx_(mtx),
    jc_(jc),
    israte_(1.0/srate),
    ignore_first_(ignore_first)
{
  pthread_mutex_init(&drawlock,NULL);
  pthread_mutex_init(&plotdatalock,NULL);
  connection_timeout = Glib::signal_timeout().connect( sigc::mem_fun(*this, &recorder_t::on_timeout), 200 );
}

void recorder_t::clear()
{
  data_.clear();
  pthread_mutex_lock( &plotdatalock );
  plotdata_.clear();
  pthread_mutex_unlock( &plotdatalock );
}

recorder_t::~recorder_t()
{
  connection_timeout.disconnect();
}

double sqr(double x)
{
  return x*x;
}

/**
   \brief Find index range corresponding to time interval
   \param data Multichannel data vector, time is in first channel
   \param channels Number of channels
   \param t1 Start time, value included
   \param t2 End time, value excluded
   \retval n1 Start index, included
   \retval n2 End index, excluded
 */
void find_timeinterval( const std::vector<double>& data, uint32_t channels, double t1, double t2, uint32_t& n1, uint32_t& n2 )
{
  uint32_t N(data.size()/channels);
  n1 = 0;
  n2 = 0;
  if( N == 0 )
    return;
  for(n2 = N-1;(n2>0)&&(data[n2*channels]>=t2);--n2);
  ++n2;
  for(n1 = n2-1;(n1>0)&&(data[n1*channels]>=t1);--n1);
}

void recorder_t::get_valuerange( const std::vector<double>& data, uint32_t channels, uint32_t firstchannel, uint32_t n1, uint32_t n2, double& dmin, double& dmax)
{
  dmin = HUGE_VAL;
  dmax = -HUGE_VAL;
  for(uint32_t dim=firstchannel;dim<channels;dim++){
    vdc[dim] = 0;
    if( !displaydc_ ){
      uint32_t cnt(0);
      for(uint32_t k=n1;k<n2;k++){
        double v(data[dim+k*channels]);
        if( (v != HUGE_VAL) && (v != -HUGE_VAL) ){
          vdc[dim]+=v;
          cnt++;
        }
      }
      if( cnt )
        vdc[dim] /= (double)cnt;
    }
    for(uint32_t k=n1;k<n2;k++){
      double v(data[dim+k*channels]);
      if( (v != HUGE_VAL) && (v != -HUGE_VAL) ){
        dmin = std::min(dmin,v-vdc[dim]);
        dmax = std::max(dmax,v-vdc[dim]);
      }
    }
  }
  if( dmax == dmin ){
    dmin -= 1.0;
    dmax += 1.0;
  }
}

bool recorder_t::on_draw(const Cairo::RefPtr<Cairo::Context>& cr)
{
  if( pthread_mutex_trylock(&drawlock) == 0 ){
    Glib::RefPtr<Gdk::Window> window = get_window();
    if(window){
      Gtk::Allocation allocation = get_allocation();
      const int width = allocation.get_width();
      const int height = allocation.get_height();
      //double ratio = (double)width/(double)height;
      cr->save();
      cr->set_source_rgb( 1.0, 1.0, 1.0 );
      cr->paint();
      cr->restore();
      cr->save();
      //cr->scale((double)width/30.0,(double)height);
      cr->translate(width,0);
      cr->set_line_width( 1 );
      // data plot:
      pthread_mutex_lock(&plotdatalock);
      double ltime(0);
      double drange(2);
      double dscale(1);
      double dmin(-1);
      double dmax(1);
      // N is number of (multi-dimensional) samples:
      uint32_t N(plotdata_.size()/size_);
      if( N > 1 ){
        //        double tmax(0);
        uint32_t n1(0);
        uint32_t n2(0);
        ltime = plotdata_[(N-1)*size_];
        find_timeinterval( plotdata_, size_, ltime-30, ltime, n1, n2 );
        get_valuerange( plotdata_, size_, 1+ignore_first_, n1, n2, dmin, dmax );
        uint32_t stepsize((n2-n1)/2048);
        ++stepsize;
        drange = dmax-dmin;
        dscale = height/drange;
        for(uint32_t dim=1+ignore_first_;dim<size_;++dim){
          cr->save();
          double size_norm(1.0/(size_-1.0-ignore_first_));
          double c_r((dim-1.0)*size_norm);
          double c_b(1.0-c_r);
          double c_g(1.0-2.0*fabs(c_r-0.5));
          cr->set_source_rgb( sqr(sqr(c_r)), sqr(c_g), sqr(sqr(c_b)) );
          bool first(true);
          // limit number of lines:
          for(uint32_t k=n1;k<n2;k+=stepsize){
            double t(plotdata_[k*size_]-ltime);
            double v(plotdata_[dim+k*size_]);
            if( !displaydc_ )
              v -= vdc[dim];
            if(v != HUGE_VAL){
              t *= width/30.0;
              v = height-(v-dmin)*dscale;
              //++nlines;
              if( first ){
                cr->move_to(t,v);
                first = false;
              }else
                cr->line_to(t,v);
            }
          }
          cr->stroke();
          cr->restore();
        }
      }
      pthread_mutex_unlock(&plotdatalock);
      // draw scale:
      cr->set_source_rgb( 0, 0, 0 );
      cr->move_to(-width,0);
      cr->line_to(0,0);
      cr->line_to(0,height);
      cr->line_to(-width,height);
      cr->line_to(-width,0);
      cr->stroke();
      // x-grid, x-scale:
      for(int32_t k=0;k<30;k++){
        double t(floor(ltime)-k);
        double x((t-ltime)*width/30.0);
        if( t >= 0 ){
          //cr->move_to(-(double)k*width/30.0,height);
          //cr->line_to(-(double)k*width/30.0,(double)height*(1-0.03));
          cr->move_to(x,height);
          double dx(0.02);
          if( (int)t % 5 == 0 )
            dx *= 2;
          cr->line_to(x,(double)height*(1-dx));
          if( (int)t % 5 == 0 ){
            char ctmp[1024];
            sprintf(ctmp,"%d",(int)t);
            cr->show_text( ctmp );
          }
        }
      }
      cr->stroke();
      // y-grid & scale:
      double ystep(pow(10.0,floor(log10(0.5*drange))));
      double ddmin(ystep*round(dmin/ystep));
      for( double dy=ddmin;dy<dmax;dy+=ystep){
        double v(height-(dy-dmin)*dscale);
        cr->move_to(-width,v);
        cr->line_to(-width*(1.0-0.01),v);
        char ctmp[1024];
        sprintf(ctmp,"%g",dy);
        cr->show_text( ctmp );
        cr->stroke();
      }
      cr->restore();
    }
    pthread_mutex_unlock(&drawlock);
  }
  return true;
}

bool recorder_t::on_timeout()
{
  Glib::RefPtr<Gdk::Window> win = get_window();
  if(win){
    Gdk::Rectangle r(0, 0, get_allocation().get_width(),
                     get_allocation().get_height());
    win->invalidate_rect(r, false);
  }
  return true;
}

void recorder_t::store(uint32_t n,double* data)
{
  if( n != size_ )
    throw TASCAR::ErrMsg("Invalid size (recorder_t::store)");
  if( pthread_mutex_trylock(&mtx_) == 0 ){
    for(uint32_t k=0;k<n;k++)
      data_.push_back(data[k]);
    if( pthread_mutex_trylock(&plotdatalock) == 0 ){
      for(uint32_t k=0;k<n;k++)
        plotdata_.push_back(data[k]);
      pthread_mutex_unlock(&plotdatalock);
    }
    pthread_mutex_unlock(&mtx_);
  }
}

void lslvar_t::poll_data()
{
  double recorder_buffer[size+1];
  double* data_buffer(&(recorder_buffer[2]));
  int ec(0);
  double t(1);
  while( t != 0 ){
    t = lsl_pull_sample_d(inlet, data_buffer, size-1, 0.0, &ec);
    if( (t != 0) && recorder && (ec == lsl_no_error) ){
      recorder_buffer[0] = t-delta + stream_delta_start;
      recorder_buffer[1] = t + stream_delta_start;
      recorder->store(size+1,recorder_buffer);
    }
  }
}

class datalogging_t : public dlog_vars_t, public TASCAR::osc_server_t {
                      //, public jackc_portless_t {
public:
  datalogging_t( const TASCAR::module_cfg_t& cfg );
  ~datalogging_t();
  void start_trial(const std::string& name);
  void stop_trial();
  void save_text(const std::string& filename);
  void save_mat(const std::string& filename);
  void save_matcell(const std::string& filename);
  void on_osc_set_trialid();
  void on_ui_showdc();
  void on_ui_start();
  void on_ui_stop();
  bool on_100ms();
  void poll_lsl_data();
  static int osc_session_start(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
  static int osc_session_stop(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
  static int osc_session_set_trialid(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data);
private:
  std::vector<recorder_t*> recorder;
  pthread_mutex_t mtx;
  bool b_recording;
  std::string filename;
  // GUI:
  Glib::RefPtr<Gtk::Builder> refBuilder;
  Gtk::Window* win;
  Gtk::Entry* trialid;
  Gtk::Label* datelabel;
  Gtk::Label* jacktime;
  Gtk::Grid* draw_grid;
  Gtk::Label* rec_label;
  Gtk::Entry* outputdirentry;
  Gtk::CheckButton* showdc;
  sigc::connection connection_timeout;
  Glib::Dispatcher osc_start;
  Glib::Dispatcher osc_stop;
  Glib::Dispatcher osc_set_trialid;
  std::string osc_trialid;
  double israte_;
  uint32_t fragsize;
  double srate;
};

#define GET_WIDGET(x) refBuilder->get_widget(#x,x);if( !x ) throw TASCAR::ErrMsg(std::string("No widget \"")+ #x + std::string("\" in builder."))


datalogging_t::datalogging_t( const TASCAR::module_cfg_t& cfg )
  : dlog_vars_t( cfg ),
    TASCAR::osc_server_t(multicast,port),
    //jackc_portless_t(jacknamer(session->name,"datalog.")),
    b_recording(false),
    israte_(1.0/cfg.session->srate),
    fragsize(cfg.session->fragsize),
    srate(cfg.session->srate)
{
  pthread_mutex_init(&mtx,NULL);
  pthread_mutex_lock(&mtx);
  TASCAR::osc_server_t* osc(this);
  if( port.empty() )
    osc = session;
  osc->add_method("/session_trialid","s",&datalogging_t::osc_session_set_trialid,this);
  osc->add_method("/session_start","",&datalogging_t::osc_session_start,this);
  osc->add_method("/session_stop","",&datalogging_t::osc_session_stop,this);
  set_jack_client( session->jc );
  for(oscvarlist_t::iterator it=vars.begin();it!=vars.end();++it){
    recorder.push_back(new recorder_t(it->size+1,it->path,mtx,session->jc,session->srate,it->ignorefirst));
    osc->add_method(it->path,it->get_fmt().c_str(),&recorder_t::osc_setvar,recorder.back());
  }
  for(lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it){
    recorder.push_back(new recorder_t((*it)->size+1,(*it)->name,mtx,session->jc,session->srate,true));
    (*it)->recorder = recorder.back();
  }
  for(std::vector<recorder_t*>::iterator it=recorder.begin();it!=recorder.end();++it)
    (*it)->set_displaydc( displaydc );
  TASCAR::osc_server_t::activate();
  refBuilder = Gtk::Builder::create_from_string(ui_datalogging);
  GET_WIDGET(win);
  GET_WIDGET(trialid);
  GET_WIDGET(datelabel);
  GET_WIDGET(jacktime);
  GET_WIDGET(draw_grid);
  GET_WIDGET(rec_label);
  GET_WIDGET(outputdirentry);
  GET_WIDGET(showdc);
  rec_label->set_text("");
  outputdir = TASCAR::env_expand(outputdir);
  outputdirentry->set_text(outputdir);
  win->set_title("tascar datalogging - " + session->name);
  int32_t dlogwin_x(0);
  int32_t dlogwin_y(0);
  int32_t dlogwin_width(300);
  int32_t dlogwin_height(300);
  win->get_size(dlogwin_width,dlogwin_height);
  win->get_position(dlogwin_x,dlogwin_y);
  get_attribute_value( e, "w", dlogwin_width );
  get_attribute_value( e, "h", dlogwin_height );
  get_attribute_value( e, "x", dlogwin_x );
  get_attribute_value( e, "y", dlogwin_y );
  win->resize( dlogwin_width, dlogwin_height );
  win->move( dlogwin_x, dlogwin_y );
  win->resize( dlogwin_width, dlogwin_height );
  win->move( dlogwin_x, dlogwin_y );
  win->show();
  //Create actions for menus and toolbars:
  Glib::RefPtr<Gio::SimpleActionGroup> refActionGroupMain = Gio::SimpleActionGroup::create();
  refActionGroupMain->add_action("start",sigc::mem_fun(*this, &datalogging_t::on_ui_start));
  refActionGroupMain->add_action("stop",sigc::mem_fun(*this, &datalogging_t::on_ui_stop));
  refActionGroupMain->add_action("showdc",sigc::mem_fun(*this, &datalogging_t::on_ui_showdc));
  osc_start.connect(sigc::mem_fun(*this,&datalogging_t::on_ui_start));
  osc_stop.connect(sigc::mem_fun(*this,&datalogging_t::on_ui_stop));
  osc_set_trialid.connect(sigc::mem_fun(*this,&datalogging_t::on_osc_set_trialid));
  win->insert_action_group("datalog",refActionGroupMain);
  connection_timeout = Glib::signal_timeout().connect( sigc::mem_fun(*this, &datalogging_t::on_100ms), 100 );
  uint32_t num_cols(ceil(sqrt(recorder.size())));
  uint32_t num_rows(ceil(recorder.size()/std::max(1u,num_cols)));
  for(uint32_t k=0;k<num_cols;k++)
    draw_grid->insert_column(0);
  for(uint32_t k=0;k<num_rows;k++)
    draw_grid->insert_row(0);
  for(uint32_t k=0;k<recorder.size();k++){
    uint32_t kc(k % num_cols);
    uint32_t kr(k / num_cols);
    Gtk::Box* box = new Gtk::Box(Gtk::ORIENTATION_VERTICAL);
    draw_grid->attach(*box,kc,kr,1,1);
    Gtk::Label* label = new Gtk::Label(recorder[k]->get_name());
    box->pack_start(*label,Gtk::PACK_SHRINK);
    box->pack_start(*(recorder[k]));
  }
  draw_grid->show_all();
  showdc->set_active(displaydc);
}

void datalogging_t::poll_lsl_data()
{
  for(lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it)
    (*it)->poll_data();
}

int datalogging_t::osc_session_start(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  ((datalogging_t*)user_data)->osc_start.emit();
  return 0;
}

int datalogging_t::osc_session_stop(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  ((datalogging_t*)user_data)->osc_stop.emit();
  return 0;
}

int datalogging_t::osc_session_set_trialid(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  ((datalogging_t*)user_data)->osc_trialid = std::string(&(argv[0]->s));
  ((datalogging_t*)user_data)->osc_set_trialid.emit();
  return 0;
}

void datalogging_t::on_osc_set_trialid()
{
  trialid->set_text(osc_trialid);
}

void datalogging_t::on_ui_showdc()
{
  displaydc = showdc->get_active();
  for(std::vector<recorder_t*>::iterator it=recorder.begin();it!=recorder.end();++it)
    (*it)->set_displaydc( displaydc );
}

void datalogging_t::on_ui_start()
{
  try{
    std::string trialidtext(trialid->get_text());
    if( trialidtext.empty() ){
      trialidtext = "unnamed";
      trialid->set_text(trialidtext);
    }
    std::string datestr_(datestr());
    datelabel->set_text(datestr_);
    //throw TASCAR::ErrMsg("Empty trial ID string.");
    trialidtext += "_" + datestr_;
    win->set_title("tascar datalogging - " + session->name + " ["+trialidtext+"]");
    start_trial(trialidtext);
    rec_label->set_text(" REC ");
  }
  catch(const std::exception& e){
    std::string tmp(e.what());
    tmp+=" (start)";
    error_message(tmp);
  }
}

void datalogging_t::on_ui_stop()
{
  try{
    stop_trial();
    rec_label->set_text("");
    win->set_title("tascar datalogging - " + session->name);
  }
  catch(const std::exception& e){
    std::string tmp(e.what());
    tmp+=" (stop)";
    error_message(tmp);
  }
}

bool datalogging_t::on_100ms()
{
  // poll LSL data:
  poll_lsl_data();
  // update labels:
  if( !b_recording )
    datelabel->set_text(datestr());
  char ctmp[1024];
  //sprintf(ctmp,"%1.1f s", jack_frame_time(jc)/(double)srate );
  sprintf(ctmp,"%1.1f s", session->tp_get_time() );
  jacktime->set_text(ctmp);
  return true;
}

int recorder_t::osc_setvar(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *user_data)
{
  double data[argc+1];
  data[0] = 0;
  for(uint32_t k=0;k<(uint32_t)argc;k++)
    data[k+1] = argv[k]->f;
  ((recorder_t*)user_data)->osc_setvar(path,argc+1,data);
  return 0;
}

void recorder_t::osc_setvar(const char *path, uint32_t size, double* data)
{
  data[0] = israte_*jack_get_current_transport_frame(jc_);
  store(size,data);
}

datalogging_t::~datalogging_t()
{
  try{
    stop_trial();
  }
  catch( const std::exception& e ){
    std::cerr << "Warning: " << e.what() << "\n";
  }
  connection_timeout.disconnect();
  TASCAR::osc_server_t::deactivate();
  for(uint32_t k=0;k<recorder.size();k++)
    delete recorder[k];
  pthread_mutex_destroy(&mtx);
  delete win;
  delete trialid;
  delete datelabel;
  delete jacktime;
  delete draw_grid;
  delete rec_label;
}

void datalogging_t::start_trial(const std::string& name)
{
  stop_trial();
  if( name.empty() )
    throw TASCAR::ErrMsg("Empty trial name.");
  session->tp_stop();
  session->tp_locate(0u);
  uint32_t timeout(1000);
  while( timeout && (session->tp_get_time() > 0) )
    usleep(1000);
  for(uint32_t k=0;k<recorder.size();k++)
    recorder[k]->clear();
  // lsl re-sync:
  for( lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it){
    (*it)->get_stream_delta_start();
  }
  filename = name;
  b_recording = true;
  pthread_mutex_unlock(&mtx);
  session->tp_start();
}

void datalogging_t::stop_trial()
{
  session->tp_stop();
  // lsl re-sync:
  for( lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it){
    (*it)->get_stream_delta_end();
  }
  outputdir = outputdirentry->get_text();
  if( !outputdir.empty() ){
    if( outputdir[outputdir.size()-1] != '/' )
      outputdir = outputdir + "/";
    struct stat info;
    if( stat( outputdir.c_str(), &info ) != 0 )
      throw TASCAR::ErrMsg("Cannot access output directory \""+outputdir+"\"." );
    if( !(info.st_mode & S_IFDIR) )  // S_ISDIR() doesn't exist on my windows 
      throw TASCAR::ErrMsg("\""+outputdir+"\" is not a directory." );
  }
  if( !b_recording )
    return;
  pthread_mutex_lock(&mtx);
  b_recording = false;
  if( fileformat == "txt" )
    save_text(filename);
  else if(fileformat == "mat" )
    save_mat(filename);
  else if(fileformat == "matcell" )
    save_matcell(filename);
}

void datalogging_t::save_text(const std::string& filename)
{
  std::string fname(outputdir+nice_name(filename,"txt"));
  std::ofstream ofs(fname.c_str());
  if( !ofs.good() )
    throw TASCAR::ErrMsg("Unable to create file \""+fname+"\".");
  ofs << "# tascar datalogging\n";
  ofs << "# version: " << TASCARVERSION << "\n";
  ofs << "# trialid: " << filename << "\n";
  ofs << "# filename: " << fname << "\n";
  ofs << "# savedat: " << datestr() << "\n";
  ofs << "# tscfilename: " << session->get_file_name() << "\n";
  ofs << "# tscpath: " << session->get_session_path() << "\n";
  ofs << "# srate: " << srate << "\n";
  ofs << "# fragsize: " << fragsize << "\n";
  for(uint32_t k=0;k<recorder.size();k++){
    ofs << "# " << recorder[k]->get_name() << std::endl;
    std::vector<double> data(recorder[k]->get_data());
    uint32_t N(recorder[k]->get_size());
    uint32_t M(data.size());
    uint32_t cnt(N-1);
    for(uint32_t l=0;l<M;l++){
      ofs << data[l];
      if( cnt-- ){
	ofs << " ";
      }else{
	ofs << "\n";
	cnt = N-1;
      }	
    }
  }
  for(lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it){
    ofs << "# " << (*it)->recorder->get_name() << "_stream_delta_start: " << (*it)->stream_delta_start << "\n";
    ofs << "# " << (*it)->recorder->get_name() << "_stream_delta_end: " << (*it)->stream_delta_end << "\n";
  }
}

void mat_add_strvar( mat_t* matfb, const std::string& name, const std::string& str)
{
  size_t dims[2];
  dims[0] = 1;
  dims[1] = str.size();
  char* v((char*)str.c_str());
  matvar_t* mStr(Mat_VarCreate(name.c_str(),MAT_C_CHAR,MAT_T_INT8,2,dims,v,0));
  if( mStr == NULL )
    throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
  Mat_VarWrite(matfb,mStr,MAT_COMPRESSION_NONE);
  Mat_VarFree(mStr);
}

void mat_add_double( mat_t* matfb, const std::string& name, double v)
{
  size_t dims[2];
  dims[0] = 1;
  dims[1] = 1;
  matvar_t* mVar(Mat_VarCreate(name.c_str(),MAT_C_DOUBLE,MAT_T_DOUBLE,2,dims,&v,0));
  if( mVar == NULL )
    throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
  Mat_VarWrite(matfb,mVar,MAT_COMPRESSION_NONE);
  Mat_VarFree(mVar);
}

void mat_add_double_field( matvar_t* s, const std::string& name, double v )
{
  Mat_VarAddStructField(s,name.c_str());
  size_t dims[2];
  dims[0] = 1;
  dims[1] = 1;
  matvar_t* mVar(Mat_VarCreate(NULL,MAT_C_DOUBLE,MAT_T_DOUBLE,2,dims,&v,0));
  if( mVar == NULL )
    throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
  Mat_VarSetStructFieldByName(s,name.c_str(),0,mVar);
}

void mat_add_char_field( matvar_t* s, const std::string& name, const std::string& str )
{
  Mat_VarAddStructField(s,name.c_str());
  size_t dims[2];
  dims[0] = 1;
  dims[1] = str.size();
  char* v((char*)str.c_str());
  matvar_t* mStr(Mat_VarCreate(NULL,MAT_C_CHAR,MAT_T_INT8,2,dims,v,0));
  if( mStr == NULL )
    throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
  Mat_VarSetStructFieldByName(s,name.c_str(),0,mStr);
}

void datalogging_t::save_mat(const std::string& filename)
{
  mat_t *matfp;
  std::string fname(outputdir+nice_name(filename,"mat"));
  matfp = Mat_CreateVer(fname.c_str(),NULL,MAT_FT_MAT5);
  if ( NULL == matfp )
    throw TASCAR::ErrMsg("Unable to create file \""+fname+"\".");
  try{
    mat_add_strvar(matfp,"tascarversion",TASCARVERSION);
    mat_add_strvar(matfp,"trialid",filename);
    mat_add_strvar(matfp,"filename",fname);
    mat_add_strvar(matfp,"savedat",datestr());
    mat_add_strvar(matfp,"tscfilename",session->get_file_name());
    mat_add_strvar(matfp,"tscpath",session->get_session_path());
    mat_add_strvar(matfp,"sourcexml",session->doc->write_to_string_formatted());
    mat_add_double(matfp,"fragsize",fragsize);
    mat_add_double(matfp,"srate",srate);
    size_t dims[2];
    for(uint32_t k=0;k<recorder.size();k++){
      std::string name(nice_name(recorder[k]->get_name()));
      std::vector<double> data(recorder[k]->get_data());
      uint32_t N(recorder[k]->get_size());
      uint32_t M(data.size());
      //uint32_t cnt(N-1);
      dims[1] = M/N;
      dims[0] = N;
      double* x(&(data[0]));
      matvar_t* mvar(Mat_VarCreate(name.c_str(),MAT_C_DOUBLE,MAT_T_DOUBLE,2,dims,x,0));
      if( mvar == NULL )
        throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
      Mat_VarWrite(matfp,mvar,MAT_COMPRESSION_NONE);
      Mat_VarFree(mvar);
    }
    for(lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it){
      mat_add_double(matfp,
                     (*it)->recorder->get_name()+"_stream_delta_start",
                     (*it)->stream_delta_start);
      mat_add_double(matfp,
                     (*it)->recorder->get_name()+"_stream_delta_end",
                     (*it)->stream_delta_end);
      char *tmp = lsl_get_xml((*it)->info);
      mat_add_strvar(matfp,
                     (*it)->recorder->get_name()+"_info",
                     tmp);
      lsl_destroy_string(tmp);
    }
  }
  catch( ... ){
    Mat_Close(matfp);
    throw;
  }
  Mat_Close(matfp);
}

void datalogging_t::save_matcell(const std::string& filename)
{
  mat_t *matfp;
  std::string fname(outputdir+nice_name(filename,"mat"));
  matfp = Mat_CreateVer(fname.c_str(),NULL,MAT_FT_MAT5);
  if ( NULL == matfp )
    throw TASCAR::ErrMsg("Unable to create file \""+fname+"\".");
  try{
    mat_add_strvar(matfp,"tascarversion",TASCARVERSION);
    mat_add_strvar(matfp,"trialid",filename);
    mat_add_strvar(matfp,"filename",fname);
    mat_add_strvar(matfp,"savedat",datestr());
    mat_add_strvar(matfp,"tscfilename",session->get_file_name());
    mat_add_strvar(matfp,"tscpath",session->get_session_path());
    mat_add_strvar(matfp,"sourcexml",session->doc->write_to_string_formatted());
    mat_add_double(matfp,"fragsize",fragsize);
    mat_add_double(matfp,"srate",srate);
    size_t dims[2];
    dims[0] = recorder.size();
    dims[1] = 1;
    matvar_t *cell_array(Mat_VarCreate("data",MAT_C_CELL,MAT_T_CELL,2,dims,NULL,0));
    if ( cell_array == NULL )
      throw TASCAR::ErrMsg("Unable to create data cell array.");
    matvar_t *cell_array_names(Mat_VarCreate("names",MAT_C_CELL,MAT_T_CELL,2,dims,NULL,0));
    if ( cell_array == NULL )
      throw TASCAR::ErrMsg("Unable to create data cell array.");
    for(uint32_t k=0;k<recorder.size();k++){
      std::string name(nice_name(recorder[k]->get_name()));
      std::vector<double> data(recorder[k]->get_data());
      uint32_t N(recorder[k]->get_size());
      uint32_t M(data.size());
      //uint32_t cnt(N-1);
      //matvar_t* mvar(Mat_VarCreate(name.c_str(),MAT_C_DOUBLE,MAT_T_DOUBLE,2,dims,x,0));
      const char *fieldnames[2] = {"data","name"};
      dims[0] = 1;
      dims[1] = 1;
      matvar_t* matDataStruct(Mat_VarCreateStruct(NULL,2,dims,fieldnames,2));
      if( matDataStruct == NULL )
        throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
      dims[1] = M/N;
      dims[0] = N;
      double* x(&(data[0]));
      matvar_t* mData(Mat_VarCreate(NULL,MAT_C_DOUBLE,MAT_T_DOUBLE,2,dims,x,0));
      if( mData == NULL )
        throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
      Mat_VarSetStructFieldByName(matDataStruct,"data",0,mData);
      dims[0] = 1;
      dims[1] = name.size();
      char* v((char*)name.c_str());
      matvar_t* mStr(Mat_VarCreate(NULL,MAT_C_CHAR,MAT_T_INT8,2,dims,v,0));
      if( mStr == NULL )
        throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
      Mat_VarSetStructFieldByName(matDataStruct,"name",0,mStr);
      // test if this is an LSL variable, if yes, provide some header information:
      // here would come some header information...
      for(lslvarlist_t::iterator it=lslvars.begin();it!=lslvars.end();++it)
        if( (*it)->recorder == recorder[k] ){
          mat_add_char_field(matDataStruct,"lsl_name",lsl_get_name((*it)->info));
          mat_add_char_field(matDataStruct,"lsl_type",lsl_get_type((*it)->info));
          mat_add_double_field(matDataStruct,"lsl_srate",lsl_get_nominal_srate((*it)->info));
          mat_add_char_field(matDataStruct,"lsl_source_id",lsl_get_source_id((*it)->info));
          mat_add_char_field(matDataStruct,"lsl_hostname",lsl_get_hostname((*it)->info));
          mat_add_double_field(matDataStruct,"lsl_protocolversion",lsl_get_version((*it)->info));
          mat_add_double_field(matDataStruct,"lsl_libraryversion",lsl_library_version());
          mat_add_double_field(matDataStruct,"stream_delta_start",(*it)->stream_delta_start);
          mat_add_double_field(matDataStruct,"stream_delta_end",(*it)->stream_delta_end);
          char *tmp = lsl_get_xml((*it)->info);
          mat_add_char_field(matDataStruct,"lsl_info",tmp);
          lsl_destroy_string(tmp);
        }
      // here add var to cell array!
      Mat_VarSetCell(cell_array,k,matDataStruct);
      matvar_t* mStrName(Mat_VarCreate(NULL,MAT_C_CHAR,MAT_T_INT8,2,dims,v,0));
      if( mStrName == NULL )
        throw TASCAR::ErrMsg("Unable to create variable \""+name+"\".");
      Mat_VarSetCell(cell_array_names,k,mStrName);
    }
    // add data cell array to Mat file:
    Mat_VarWrite(matfp,cell_array,MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp,cell_array_names,MAT_COMPRESSION_NONE);
    Mat_VarFree(cell_array);
    Mat_VarFree(cell_array_names);
  }
  catch( ... ){
    Mat_Close(matfp);
    throw;
  }
  Mat_Close(matfp);
}

REGISTER_MODULE(datalogging_t);

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */

