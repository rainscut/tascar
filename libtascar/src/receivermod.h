/**
 * @file receivermod.h
 * @brief Base classes for receiver module definitions
 * @ingroup libtascar
 * @author Giso Grimm
 * @date 2014,2017
 */
/* License (GPL)
 *
 * Copyright (C) 2018  Giso Grimm
 *
 * This program is free software; you can redistribute it and/or
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

#ifndef RECEIVERMOD_H
#define RECEIVERMOD_H

#include "speakerarray.h"
#include "tascarplugin.h"

namespace TASCAR {

  class receivermod_base_t : public xml_element_t, public audiostates_t {
  public:
    class data_t {
    public:
      data_t(){};
      virtual ~data_t(){};
    };
    /**
       \brief Constructor, mainly parsing of configuration.
     */
    receivermod_base_t( xmlpp::Element* xmlsrc );
    virtual ~receivermod_base_t();
    /**
       \brief Add content of a single sound source (primary or image source)
       
       This method implements the core rendering function (receiver characteristics and render format).

       \param prel Position of the source in the receiver coordinate system (from the receiver perspective).
       \param width Source width.
       \param chunk Input audio signal, as returned from the source characteristics and transmission model.
       \retval output Output audio containers.
       \param sd State data of each acoustic model instance (see TASCAR::Acousticmodel::receiver_graph_t and TASCAR::Acousticmodel::world_t for details)
     */
    virtual void add_pointsource( const pos_t& prel, double width, const wave_t& chunk, std::vector<wave_t>& output, receivermod_base_t::data_t* sd ) = 0;
    virtual void add_diffuse_sound_field( const amb1wave_t& chunk, std::vector<wave_t>& output, receivermod_base_t::data_t* ) = 0;
    virtual void postproc( std::vector<wave_t>& output ) {};
    virtual uint32_t get_num_channels() = 0;
    virtual std::string get_channel_postfix( uint32_t channel ) const { return "";};
    virtual std::vector<std::string> get_connections() const { return std::vector<std::string>();};
    virtual receivermod_base_t::data_t* create_data( double srate,uint32_t fragsize ) { return NULL;};
    virtual void add_variables( TASCAR::osc_server_t* srv ) {};
  protected:
  };

  /**
     \brief Base class for loudspeaker based render formats
   */
  class receivermod_base_speaker_t : public receivermod_base_t {
  public:
    receivermod_base_speaker_t( xmlpp::Element* xmlsrc );
    virtual std::vector<std::string> get_connections() const;
    virtual void postproc( std::vector<wave_t>& output );
    virtual void prepare( chunk_cfg_t& );
    virtual void release();
    void add_diffuse_sound_field( const amb1wave_t& chunk, std::vector<wave_t>& output, receivermod_base_t::data_t* );
    uint32_t get_num_channels();
    std::string get_channel_postfix(uint32_t channel) const;
    virtual void add_variables( TASCAR::osc_server_t* srv );
    virtual void validate_attributes(std::string&) const;
    TASCAR::spk_array_diff_render_t spkpos;
  };

  class receivermod_t : public receivermod_base_t {
  public:
    receivermod_t( xmlpp::Element* xmlsrc );
    virtual ~receivermod_t();
    virtual void add_pointsource( const pos_t& prel, double width, const wave_t& chunk, std::vector<wave_t>& output, receivermod_base_t::data_t* );
    virtual void add_diffuse_sound_field( const amb1wave_t& chunk, std::vector<wave_t>& output, receivermod_base_t::data_t* );
    virtual void postproc( std::vector<wave_t>& output );
    virtual uint32_t get_num_channels();
    virtual std::string get_channel_postfix( uint32_t channel );
    virtual std::vector<std::string> get_connections() const;
    void prepare( chunk_cfg_t& );
    void release();
    virtual receivermod_base_t::data_t* create_data( double srate,uint32_t fragsize );
    virtual void add_variables( TASCAR::osc_server_t* srv );
  private:
    receivermod_t( const receivermod_t& );
    std::string receivertype;
    void* lib;
  public:
    TASCAR::receivermod_base_t* libdata;
  };

}

#define REGISTER_RECEIVERMOD(x) TASCAR_PLUGIN( receivermod_base_t, xmlpp::Element*, x )

#endif

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * compile-command: "make -C .."
 * End:
 */
