/**
 * @file flowtable.cpp Edge Proxy flow table maintenance
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

extern "C" {
#include <pjsip.h>
#include <pjlib-util.h>
#include <pjlib.h>
}

// Common STL includes.
#include <cassert>
#include <map>
#include <string>

#include "log.h"
#include "utils.h"
#include "pjutils.h"
#include "stack.h"
#include "flowtable.h"


FlowTable::FlowTable() :
  _tp2flow_map(),
  _tk2flow_map(),
  _statistic("client_count")
{
  pthread_mutex_init(&_flow_map_lock, NULL);
  report_flow_count();
}


FlowTable::~FlowTable()
{
  // Delete all the existing flows.
  for (std::map<FlowKey, Flow*>::iterator i = _tp2flow_map.begin();
       i != _tp2flow_map.end();
       ++i)
  {
    delete i->second;
  }

  pthread_mutex_destroy(&_flow_map_lock);
}


/// Find or create a flow corresponding to the specified transport and remote
/// IP address and port. This is a single method to ensure it is atomic.
Flow* FlowTable::find_create_flow(pjsip_transport* transport, const pj_sockaddr* raddr)
{
  Flow* flow = NULL;
  FlowKey key(transport->key.type, raddr);

  char buf[100];
  LOG_DEBUG("Find or create flow for transport %s (%d), remote address %s",
            transport->obj_name, transport->key.type,
            pj_sockaddr_print(raddr, buf, sizeof(buf), 3));

  pthread_mutex_lock(&_flow_map_lock);

  std::map<FlowKey, Flow*>::iterator i = _tp2flow_map.find(key);

  if (i == _tp2flow_map.end())
  {
    // No matching flow, so create a new one.
    flow = new Flow(this, transport, raddr);

    // Add the new flow to the maps.
    _tp2flow_map.insert(std::make_pair(key, flow));
    _tk2flow_map.insert(std::make_pair(flow->token(), flow));

    LOG_DEBUG("Added flow record %p", flow);

    report_flow_count();
  }
  else
  {
    // Found a matching flow, so return this one.
    flow = i->second;

    LOG_DEBUG("Found flow record %p", flow);
  }

  // Add a reference to the flow.
  flow->inc_ref();

  pthread_mutex_unlock(&_flow_map_lock);

  return flow;
}


/// Find the flow corresponding to the specified transport and remote IP
/// address and port.
Flow* FlowTable::find_flow(pjsip_transport* transport, const pj_sockaddr* raddr)
{
  Flow* flow = NULL;
  FlowKey key(transport->key.type, raddr);

  char buf[100];
  LOG_DEBUG("Find flow for transport %s (%d), remote address %s",
            transport->obj_name, transport->key.type,
            pj_sockaddr_print(raddr, buf, sizeof(buf), 3));

  pthread_mutex_lock(&_flow_map_lock);

  std::map<FlowKey, Flow*>::iterator i = _tp2flow_map.find(key);

  if (i != _tp2flow_map.end())
  {
    // Found a matching flow, so return this one.
    flow = i->second;

    // Increment the reference count on the flow.
    flow->inc_ref();

    LOG_DEBUG("Found flow record %p", flow);
  }

  pthread_mutex_unlock(&_flow_map_lock);

  return flow;
}


/// Find the flow corresponding to the specified flow token.
Flow* FlowTable::find_flow(const std::string& token)
{
  Flow* flow = NULL;

  LOG_DEBUG("Find flow for flow token %s", token.c_str());

  pthread_mutex_lock(&_flow_map_lock);

  std::map<std::string, Flow*>::iterator i = _tk2flow_map.find(token);
  if (i != _tk2flow_map.end())
  {
    // Found a flow matching the token.
    flow = i->second;

    // Add a reference to the flow.
    flow->inc_ref();

    LOG_DEBUG("Found flow record %p", flow);
  }

  pthread_mutex_unlock(&_flow_map_lock);

  return flow;
}


void FlowTable::remove_flow(Flow* flow)
{
  pthread_mutex_lock(&_flow_map_lock);

  LOG_DEBUG("Remove flow %p", flow);

  FlowKey key(flow->transport()->key.type, flow->remote_addr());

  std::map<FlowKey, Flow*>::iterator i = _tp2flow_map.find(key);
  if (i != _tp2flow_map.end())
  {
    _tp2flow_map.erase(i);
  }

  std::map<std::string, Flow*>::iterator j = _tk2flow_map.find(flow->token());
  if (j != _tk2flow_map.end())
  {
    _tk2flow_map.erase(j);
  }

  report_flow_count();

  delete flow;

  pthread_mutex_unlock(&_flow_map_lock);
}


void FlowTable::report_flow_count()
{
  LOG_DEBUG("Reporting current flow count: %d", _tp2flow_map.size());
  std::vector<std::string> message;
  message.push_back(std::to_string(_tp2flow_map.size()));
  _statistic.report_change(message);
}


Flow::Flow(FlowTable* flow_table, pjsip_transport* transport, const pj_sockaddr* remote_addr) :
  _flow_table(flow_table),
  _transport(transport),
  _remote_addr(*remote_addr),
  _token(),
  _authorized_ids(),
  _default_id(),
  _refs(1)
{
  // Create the lock for protecting the authorized_ids and default_id.
  pthread_mutex_init(&_flow_lock, NULL);

  // Create a random base64 encoded token for the flow.
  PJUtils::create_random_token(Flow::TOKEN_LENGTH, _token);

  if (PJSIP_TRANSPORT_IS_RELIABLE(_transport))
  {
    // We're adding a new reliable transport, so make sure it stays around
    // until we remove it from the map.
    pjsip_transport_add_ref(transport);

    // Add a state listener so we find out when the flow is destroyed.
    pjsip_tp_state_listener_key* listener_key;
    pjsip_transport_add_state_listener(transport,
                                       &on_transport_state_changed,
                                       this,
                                       &listener_key);
    LOG_DEBUG("Added transport listener for flow %p", this);
  }

  // Initialize the timer.
  pj_timer_entry_init(&_timer, PJ_FALSE, (void*)this, &on_timer_expiry);
  _timer.id = 0;

  // Start the timer as an idle timer.
  restart_timer(IDLE_TIMER, IDLE_TIMEOUT);
}


Flow::~Flow()
{
  if (PJSIP_TRANSPORT_IS_RELIABLE(_transport))
  {
    // We incremented the ref count when we put it in the map.
    pjsip_transport_dec_ref(_transport);
  }

  if (_timer.id)
  {
    // Stop the keepalive timer.
    pjsip_endpt_cancel_timer(stack_data.endpt, &_timer);
    _timer.id = 0;
  }

  pthread_mutex_destroy(&_flow_lock);
}


/// Returns the full asserted identity corresponding to the specified
/// preferred identity, or an empty string if the preferred identity is not
/// authorized on this flow.
std::string Flow::asserted_identity(pjsip_uri* preferred_identity)
{
  std::string aor = PJUtils::public_id_from_uri((pjsip_uri*)pjsip_uri_get_uri(preferred_identity));
  std::string id;

  pthread_mutex_lock(&_flow_lock);

  auth_id_map::const_iterator i = _authorized_ids.find(aor);

  if (i != _authorized_ids.end())
  {
    // Found the corresponding identity.
    id = i->second.name_addr;
  }

  pthread_mutex_unlock(&_flow_lock);

  return id;
}


/// Returns a default identity for this flow.  Note that a single flow
/// may have multiple default identities.  Return an empty string if no
/// identities are authorized on this flow.
std::string Flow::default_identity()
{
  pthread_mutex_lock(&_flow_lock);

  std::string id = _default_id;

  pthread_mutex_unlock(&_flow_lock);

  return id;
}


/// Sets the specified identities as authorized for this flow.
void Flow::set_identity(const pjsip_uri* uri, bool is_default, int expires)
{
  int now = time(NULL);

  // Render the URI to an AoR suitable to look up in the map.
  std::string aor = PJUtils::public_id_from_uri((pjsip_uri*)pjsip_uri_get_uri(uri));

  LOG_DEBUG("Setting identity %s on flow %p, expires = %d", aor.c_str(), this, expires);

  pthread_mutex_lock(&_flow_lock);

  // Convert the expiry time to an absolute time.
  expires += now;

  if (expires > now)
  {
    // Find or create the entry for this aor.
    AuthId& aid = _authorized_ids[aor];

    // Store the name_addr rendered from the received URI.
    aid.name_addr = PJUtils::uri_to_string(PJSIP_URI_IN_FROMTO_HDR, uri);

    // Update the expiry time.
    aid.expires = expires;

    // Set the default_id flag
    aid.default_id = is_default;

    if ((aid.default_id) && (_default_id == ""))
    {
      // This is the first default_id to be set.
      _default_id = aor;
    }

    // May need to (re)start the timer if either it's not running, or it's
    // running as an idle timer, or the expires time for these identities is
    // earlier than the timer will next pop.
    if ((_timer.id != EXPIRY_TIMER) ||
        (_timer._timer_value.sec > expires))
    {
      restart_timer(EXPIRY_TIMER, expires - time(NULL));
    }
  }
  else
  {
    LOG_DEBUG("Deleting identity %s", aor.c_str());
    auth_id_map::iterator i = _authorized_ids.find(aor);

    // Check to see whether this was the current default identity we are
    // using for this flow.  We could do the string comparision in all cases
    // but it is only necessary if the identity is marked as a default
    // candidate.
    if ((i->second.default_id) && (i->first == _default_id))
    {
      // This was our default ID, so remove it.
      _default_id = "";
    }
    _authorized_ids.erase(i);

    if (_default_id == "")
    {
      // We've lost our default identity, so scan the list to see if there is
      // another one we can use.
      select_default_identity();
    }

    // No need to restart the timer here.  It may pop earlier than necessary
    // next time (if the entry we just deleted was the first to expire) but
    // that won't cause any problems.  Restarting it here would require
    // a scan through all the entries looking for the next one to expire,
    // so would be no more efficient.
  }

  pthread_mutex_unlock(&_flow_lock);
}


/// Called when the expiry timer pops.
void Flow::expiry_timer()
{
  // Scan through all the identities deleting any that have passed their
  // expiry time.  This is done as a simple scan because we don't expect
  // a single flow to have a large number of identities.  This may not be
  // a valid assumption if a downstream SBC or AGCF muxes a large number of
  // clients over a single flow.
  pthread_mutex_lock(&_flow_lock);

  int now = time(NULL);
  int min_expires = 0;
  for (auth_id_map::const_iterator i = _authorized_ids.begin();
       i != _authorized_ids.end();
       ++i)
  {
    if (i->second.expires <= now)
    {
      LOG_DEBUG("Expiring identity %s", i->first.c_str());

      // Check to see whether this was the current default identity we are
      // using for this flow.  We could do the string comparision in all cases
      // but it is only necessary if the identity is marked as a default
      // candidate.
      if ((i->second.default_id) && (i->first == _default_id))
      {
        // This was our default ID, so remove it.
        _default_id = "";
      }
      _authorized_ids.erase(i);
    }
    else
    {
      // This entry hasn't expired yet, so use to to work out when we next
      // need the expiry timer to pop.
      if ((min_expires == 0) || (i->second.expires < min_expires))
      {
        min_expires = i->second.expires;
      }
    }
  }

  if (_default_id == "")
  {
    // We've lost our default identity, so scan the list to see if there is
    // another one we can use.
    select_default_identity();
  }

  if ((_authorized_ids.size() == 0) &&
      (!PJSIP_TRANSPORT_IS_RELIABLE(_transport)))
  {
    // No active registrations on a non-reliable transport, so restart the
    // timer as an idle timer.
    restart_timer(IDLE_TIMER, IDLE_TIMEOUT);
    LOG_DEBUG("Started idle timer for flow %p", this);
  }
  else if (min_expires > now)
  {
    // Restart the timer to pop when the next identity(s) will expire.
    restart_timer(EXPIRY_TIMER, min_expires - now);
  }

  pthread_mutex_unlock(&_flow_lock);
}


/// Scan the list of identity for a default candidate.
void Flow::select_default_identity()
{
  for (auth_id_map::const_iterator i = _authorized_ids.begin();
       i != _authorized_ids.end();
       ++i)
  {
    if (i->second.default_id)
    {
      // Found a candidate default identity.
      _default_id = i->first;
    }
  }
}


/// Restart the timer using the specified id and timeout.
void Flow::restart_timer(int id, int timeout)
{
  if (_timer.id)
  {
    // Stop the existing timer.
    pjsip_endpt_cancel_timer(stack_data.endpt, &_timer);
    _timer.id = 0;
  }

  pj_time_val delay = {timeout, 0};
  pjsip_endpt_schedule_timer(stack_data.endpt, &_timer, &delay);
  _timer.id = id;
}


/// Increment the reference count on the flow.  This is always called when
/// the flowtable lock is held, so no need to lock.
void Flow::inc_ref()
{
  ++_refs;
}


/// Decrements the reference count on the flow, and suicides if it gets
/// to zero.
void Flow::dec_ref()
{
  pthread_mutex_lock(&_flow_table->_flow_map_lock);

  if ((--_refs) == 0)
  {
    pthread_mutex_unlock(&_flow_table->_flow_map_lock);
    _flow_table->remove_flow(this);
  }
  else
  {
    pthread_mutex_unlock(&_flow_table->_flow_map_lock);
  }
}


/// Called by PJSIP when a reliable transport connection changes state.
void Flow::on_transport_state_changed(pjsip_transport *tp,
                                      pjsip_transport_state state,
                                      const pjsip_transport_state_info *info)
{
  LOG_DEBUG("Transport state changed for flow %p, state = %d",
            info->user_data, state);
  if (state == PJSIP_TP_STATE_DISCONNECTED)
  {
    // Transport connection has disconnected, so decrement the reference count
    // so it will eventually get removed from the map.
    ((Flow*)(info->user_data))->dec_ref();
  }
}


/// Called by PJSIP when the expiry/idle timer expires.
void Flow::on_timer_expiry(pj_timer_heap_t *th, pj_timer_entry *e)
{
  LOG_DEBUG("%s timer expired for flow %p",
            (e->id == EXPIRY_TIMER) ? "Expiry" : "Idle",
            e->user_data);
  if (e->id == EXPIRY_TIMER)
  {
    // Timer is an expiry timer.
    ((Flow*)e->user_data)->expiry_timer();
  }
  else
  {
    // Timer is an idle timer, so decrement the reference count so the flow
    // will get deleted when there are no more references.
    ((Flow*)e->user_data)->dec_ref();
  }
}

