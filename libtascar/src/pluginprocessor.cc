#include "pluginprocessor.h"

using namespace TASCAR;

plugin_processor_t::plugin_processor_t( xmlpp::Element* xmlsrc, const std::string& name, const std::string& parentname )
  : xml_element_t( xmlsrc )
{
  xmlpp::Element* se_plugs(find_or_add_child("plugins"));
  xmlpp::Node::NodeList subnodes(se_plugs->get_children());
  for(xmlpp::Node::NodeList::iterator sn=subnodes.begin();sn!=subnodes.end();++sn){
    xmlpp::Element* sne(dynamic_cast<xmlpp::Element*>(*sn));
    if( sne ){
      plugins.push_back(new TASCAR::audioplugin_t( audioplugin_cfg_t(sne,name,parentname)));
    }
  }
}

void plugin_processor_t::prepare( chunk_cfg_t& cf_ )
{
  audiostates_t::prepare( cf_ );
  for( auto p=plugins.begin(); p!= plugins.end(); ++p)
    (*p)->prepare( cf_ );
}

void plugin_processor_t::add_licenses( licensehandler_t* lh )
{
  for( auto p=plugins.begin(); p!= plugins.end(); ++p)
    (*p)->add_licenses( lh );
}

void plugin_processor_t::release()
{
  audiostates_t::release();
  for( auto p=plugins.begin(); p!= plugins.end(); ++p)
    (*p)->release();
}

plugin_processor_t::~plugin_processor_t()
{
  for( auto p=plugins.begin(); p!= plugins.end(); ++p)
    delete (*p);
}

void plugin_processor_t::validate_attributes(std::string& msg) const
{
  for( auto p=plugins.begin(); p!=plugins.end(); ++p)
    (*p)->validate_attributes(msg);
}

void plugin_processor_t::process_plugins( std::vector<wave_t>& s, const pos_t& pos, const transport_t& tp )
{
  for( auto p=plugins.begin(); p!= plugins.end(); ++p)
    (*p)->ap_process( s, pos, tp );
}

void plugin_processor_t::add_variables( TASCAR::osc_server_t* srv )
{
  std::string oldpref(srv->get_prefix());
  uint32_t k=0;
  for( auto p=plugins.begin(); p!=plugins.end(); ++p){
    char ctmp[1024];
    sprintf(ctmp,"ap%d",k);
    srv->set_prefix(oldpref+"/"+ctmp+"/"+(*p)->get_modname());
    (*p)->add_variables( srv );
    ++k;
  }
  srv->set_prefix(oldpref);
}
