/**
 * @file sproutletproxy.h  Sproutlet controller proxy class definition
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef SPROUTLETPROXY_H__
#define SPROUTLETPROXY_H__

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <list>

#include "basicproxy.h"
#include "pjutils.h"
#include "sproutlet.h"
#include "snmp_counter_table.h"
#include "snmp_sip_request_types.h"
#include "sproutlet_options.h"

class SproutletWrapper;

class SproutletProxy : public BasicProxy, SproutletHelper
{
public:
  static const int DEFAULT_MAX_SPROUTLET_DEPTH = 50;

  ///
  /// @brief      Constructor
  ///
  /// @param[in]  endpt                        The pjsip endpoint to associate
  ///                                          with.
  /// @param[in]  priority                     The pjsip priority to load at.
  /// @param[in]  root_uri                     The Sprout hostname.
  /// @param[in]  host_local_aliases           Hostnames of local Sproutlets.
  /// @param[in]  host_remote_aliases          Hostnames of Sproutlets in remote
  ///                                          sites.
  /// @param[in]  always_serve_remote_aliases  Whether requests meant for remote
  ///                                          aliases should always be handled
  ///                                          locally.
  /// @param[in]  sproutlets                   Sproutlets to load in this proxy.
  /// @param[in]  stateless_proxies            A set of next-hops that are
  ///                                          considered to be stateless
  ///                                          proxies.
  /// @param[in]  route_to_remote_alias_tbl    SNMP table for counting the
  ///                                          number of requests routed to
  ///                                          remote aliases.
  /// @param[in]  accept_for_remote_alias_tbl  SNMP table for counting the
  ///                                          number of requests for remote
  ///                                          aliases accepted locally.
  /// @param[in]  max_sproutlet_depth          The maximum number of Sproutlets
  ///                                          that can be invoked in a row
  ///                                          before we break the loop.
  ///
  SproutletProxy(pjsip_endpoint* endpt,
                 int priority,
                 const std::string& root_uri,
                 const std::unordered_set<std::string>& host_local_aliases,
                 const std::unordered_set<std::string>& host_remote_aliases,
                 const bool always_serve_remote_aliases,
                 const std::list<Sproutlet*>& sproutlets,
                 const std::set<std::string>& stateless_proxies,
                 SNMP::CounterTable* route_to_remote_alias_tbl,
                 SNMP::CounterTable* accept_for_remote_alias_tbl,
                 int max_sproutlet_depth=DEFAULT_MAX_SPROUTLET_DEPTH);

  /// Destructor.
  virtual ~SproutletProxy();

  /// Static callback for timers
  static void on_timer_pop(pj_timer_heap_t* th, pj_timer_entry* tentry);

  /// Constructs the next hop URI if a Sproutlet doesn't want to handle a
  /// request.
  pjsip_sip_uri* next_hop_uri(const std::string& service,
                              const pjsip_sip_uri* base_uri,
                              pj_pool_t* pool) const;

  enum SPROUTLET_SELECTION_TYPES
  {
    SERVICE_NAME=0,
    DOMAIN_PART,
    USER_PART,
    NONE_SELECTED=1000,
  };

protected:
  /// Pre-declaration
  class UASTsx;

  // When a host is compared against the alias list, there are three different
  // match possibilities.

  ///
  /// @brief      When a host is compared against the alias list, there are
  ///             three different match possiblities.
  ///
  /// NO_MATCH represents the case that the host does not match any entry on the
  /// local or remote alias lists.
  ///
  /// REMOTE represents the case that the host matches against an entry on the
  /// remote alias list.
  ///
  /// LOCAL represents the case that the host matches against an entry on the
  /// local alias list.
  ///
  enum AliasMatchLocality
  {
    NO_MATCH,
    REMOTE,
    LOCAL
  };

  ///
  /// @brief      Utility function that converts an AliasMatchLocality into a
  ///             bool representing whether or not any match was obtained.
  ///
  /// @param[in]  match  The match locality
  ///
  /// @return     False if the match locality is NO_MATCH, and true otherwise.
  ///
  static bool is_alias_match(AliasMatchLocality match)
  {
    switch (match)
    {
      case REMOTE:
      case LOCAL:
        return true;
      case NO_MATCH:
        return false;
      default:
        return false;
    }
  }

  ///
  /// @brief      A pair of a Sproutlet (pointer) and a match locality.
  ///
  struct SproutletMatch
  {
    Sproutlet* sproutlet;
    AliasMatchLocality match_locality;
    SproutletMatch(Sproutlet* sproutlet,
                   AliasMatchLocality match_locality) :
      sproutlet(sproutlet),
      match_locality(match_locality)
    {}
  };

  ///
  /// @brief      Represents the four possible cases when attempting to obtain a
  ///             SproutletTsx.
  ///
  /// NO_MATCH_AT_ALL represents the case that neither a local nor remote match
  /// was found for the host.
  ///
  /// NO_MATCH_REJECTED_REMOTE_MATCH represents the case that a remote match was
  /// found, but was rejected. This is because
  ///   - always_serve_remote_aliases is set to false, and
  ///   - the match was on the other side of a network function or originating/
  ///     terminating S-CSCF boundary.
  ///
  /// MATCH_REMOTE represents the case that a remote match was found (and not
  /// rejected).
  ///
  /// MATCH_LOCAL represents the case that a local match was found.
  ///
  enum SproutletTsxMatchLocality
  {
    NO_MATCH_AT_ALL,
    NO_MATCH_REJECTED_REMOTE_MATCH,
    MATCH_REMOTE,
    MATCH_LOCAL,
  };

  ///
  /// @brief      A pair of a SproutletTsx (pointer) and a match locality.
  ///
  struct SproutletTsxMatch
  {
    SproutletTsx* sproutlet_tsx;
    SproutletTsxMatchLocality match_locality;
    SproutletTsxMatch(SproutletTsx* sproutlet_tsx,
                      SproutletTsxMatchLocality match_locality) :
      sproutlet_tsx(sproutlet_tsx),
      match_locality(match_locality)
    {}
  };

  /// Create Sproutlet UAS transaction objects.
  BasicProxy::UASTsx* create_uas_tsx();

  /// Registers a sproutlet.
  bool register_sproutlet(Sproutlet* sproutlet);

  ///
  /// @brief      Utility method to find the appropriate Sproutlet to handle a
  ///             request.
  ///
  /// We first attempt to match a Sproutlet using the request URI, then the
  /// service name, and then the port. For port matching, the caller-supplied
  /// port parameter is used if it is non-zero, and otherwise the port supplied
  /// on the message is used (if it is provided).
  ///
  /// @param[in]  req    The request
  /// @param[in]  port   The port on which the request was recieved
  /// @param[out] alias  The alias of the returned Sproutlet against which the
  ///                    request matched.
  /// @param[in]  trail  The SAS trail ID
  ///
  /// @return     The matched Sproutlet or NULL if no Sproutlet was found, and
  ///             the match locality, or NO_MATCH if no Sproutlet was found.
  ///
  SproutletMatch target_sproutlet(pjsip_msg* req,
                              int port,
                              std::string& alias,
                              SAS::TrailId trail);

  ///
  /// @brief      Return the Sproutlet that matches the URI supplied along with
  ///             the locality of the match.
  ///
  /// We first attempt to match a Sproutlet using the service parameter, then
  /// the domain, then the user part of the URI.
  ///
  /// @param[in]  uri             The uri against which to match.
  /// @param[out] alias           The alias of the returned Sproutlet against
  ///                             which the uri matched.
  /// @param[out] local_hostname  The hostname of the matched Sproutlet.
  /// @param      selection_type  Which of the above methods was used to obtain
  ///                             the matching Sproutlet, that is, whether it
  ///                             matched on service name, the domain, or the
  ///                             user part.
  ///
  /// @return     The matched Sproutlet or NULL if no Sproutlet was found, and
  ///             the match locality, or NO_MATCH if no Sproutlet was found.
  ///
  SproutletMatch match_sproutlet_from_uri(
    const pjsip_uri* uri,
    std::string& alias,
    std::string& local_hostname,
    SPROUTLET_SELECTION_TYPES& selection_type) const;

  /// Create a URI that routes to a given Sproutlet.
  pjsip_sip_uri* create_sproutlet_uri(pj_pool_t* pool,
                                      Sproutlet* sproutlet) const;

  /// Create a URI that routes to the named sproutlet internally within the
  /// sproutlet proxy.
  ///
  /// @param pool         - Pool to allocate the URI from.
  /// @param name         - The name of the service to invoke.
  /// @param existing_uri - An existing URI to base the new URI on.
  pjsip_sip_uri* create_internal_sproutlet_uri(pj_pool_t* pool,
                                               const std::string& name,
                                               const pjsip_sip_uri* existing_uri) const;

  Sproutlet* service_from_host(pjsip_sip_uri* uri);
  Sproutlet* service_from_user(pjsip_sip_uri* uri);
  Sproutlet* service_from_params(pjsip_sip_uri* uri);

  ///
  /// @brief      Compares the given URI to the alias lists, and returns the
  ///             match locality.
  ///
  /// Determines if the URI matches any alias, and if so, whether it is a local
  /// alias or a remote one.
  ///
  /// @param[in]  uri   The uri
  ///
  /// @return     The match locality, either LOCAL, indicating the hostname
  ///             matched a local alias, REMOTE, indicating a remote match, or
  ///             NO_MATCH, indicating that the hostname matched neither local nor
  ///             remote aliases.
  ///
  AliasMatchLocality get_uri_locality(const pjsip_uri* uri);

  pjsip_sip_uri* get_routing_uri(const pjsip_msg* req,
                                 const Sproutlet* sproutlet) const;
  std::string get_local_hostname(const pjsip_sip_uri* uri) const;

  ///
  /// @brief      Compares the given hostname to the alias lists and returns the
  ///             match locality.
  ///
  /// Determines if the hostname matches any alias, and if so, whether it is a
  /// local alias or a remote one.
  ///
  /// @param[in]  host  The hostname to use.
  ///
  /// @return     The match locality, either LOCAL, indicating the hostname
  ///             matched a local alias, REMOTE, indicating a remote match, or
  ///             NO_MATCH, indicating that the hostname matched neither local nor
  ///             remote aliases.
  ///
  AliasMatchLocality get_host_locality(const pj_str_t* host) const;

  bool is_uri_reflexive(const pjsip_uri* uri,
                        const Sproutlet* sproutlet) const;

  // A struct to wrap tx_data and allowed_host_state in a convenient bundle
  // to pass over interfaces when sending a request.
  typedef struct
  {
    pjsip_tx_data* tx_data;
    int allowed_host_state;
  } SendRequest;

  bool schedule_timer(pj_timer_entry* tentry, int duration);
  bool cancel_timer(pj_timer_entry* tentry);
  bool timer_running(pj_timer_entry* tentry);

  ///
  /// @brief      Represents the server side of a single SIP transaction.
  ///             Initialized on receipt of a SIP request, and persists for the
  ///             duration of the transaction.
  ///
  class UASTsx : public BasicProxy::UASTsx
  {
  public:
    /// Constructor.
    UASTsx(SproutletProxy* proxy);

    /// Destructor.
    virtual ~UASTsx();

    ///
    /// @brief      Initializes the UAS transaction.
    ///
    /// Called at the start of a SIP (server) transaction to initialize a
    /// transaction object from the request data.
    ///
    /// @param[in]  rdata  The request data.
    ///
    /// @return     The status of the initialization.
    ///
    virtual pj_status_t init(pjsip_rx_data* rdata);

    /// Handle the incoming half of a transaction request.
    virtual void process_tsx_request(pjsip_rx_data* rdata);

    /// Handle a received CANCEL request.
    virtual void process_cancel_request(pjsip_rx_data* rdata, const std::string& reason);

    /// Handle a timer pop.
    static void on_timer_pop(pj_timer_heap_t* th, pj_timer_entry* tentry);

  protected:
    /// Handles a response to an associated UACTsx.
    virtual void on_new_client_response(UACTsx* uac_tsx,
                                        pjsip_tx_data *tdata);

    /// Notification that an client transaction is not responding.
    virtual void on_client_not_responding(UACTsx* uac_tsx,
                                          ForkErrorState fork_error,
                                          const std::string& reason);

    virtual void on_tsx_state(pjsip_event* event);


  private:
    /// Defintion of a timer set by a sproutlet transaction.
    struct TimerCallbackData
    {
      UASTsx* uas_tsx;
      SproutletWrapper* sproutlet_wrapper;
      void* context;
    };

    // The timer callback object, which is run on a worker thread
    class TimerCallback : public PJUtils::Callback
    {
      pj_timer_entry* _timer_entry;

    public:
      TimerCallback(pj_timer_entry* timer);
      void run() override;
    };

    void tx_request(SproutletWrapper* sproutlet,
                    int fork_id,
                    SendRequest req);

    ///
    /// @brief      Takes requests from the pending request queue and processes
    ///             them.
    ///
    /// The request is rejected if Max-Forwards has reached zero, or has been
    /// through more than the maximum specified number of Sproutlets. Otherwise,
    /// we attempt to select a Sproutlet and create a SproutletTsx. If this
    /// succeeds, the request is passed to that Sproutlet, and if not, the
    /// request is proxied on.
    ///
    void schedule_requests();

    void process_timer_pop(pj_timer_entry* tentry);
    bool schedule_timer(SproutletWrapper* tsx, void* context, TimerID& id, int duration);
    bool cancel_timer(TimerID id);
    bool timer_running(TimerID id);

    void tx_response(SproutletWrapper* sproutlet,
                     pjsip_tx_data* rsp);

    void tx_cancel(SproutletWrapper* sproutlet,
                   int fork_id,
                   pjsip_tx_data* cancel,
                   int st_code,
                   const std::string& reason);

    /// Checks to see if it is safe to destroy the UASTsx.
    void check_destroy();

    ///
    /// @brief      Utility function to determine if a matched Sproutlet should
    ///             be accepted or rejected, based on its locality.
    ///
    /// If the match is null, that is, no Sproutlet was matched, the function
    /// always returns false. The tsx_match_locality parameter is set to
    /// NO_MATCH_AT_ALL.
    ///
    /// If the match is local, the function always returns true, and the
    /// tsx_match_locality_parameter is set to MATCH_LOCAL.
    ///
    /// If the match is remote, the function has the following behaviour.
    ///   - If the always_serve_remote_aliases flag is set, or if the matched
    ///     Sproutlet does not lie across a network function or originating/
    ///     terminating S-CSCF boundary, the function returns true. We also
    ///     produce a SAS log. The tsx_match_locality parameter is set to
    ///     MATCH_REMOTE.
    ///   - Otherwise, the function returns false, and a different SAS log is
    ///     produced. The tsx_match_locality_parameter is set to
    ///     NO_MATCH_REMOTE_MATCH_REJECTED.
    ///
    /// @param[in]  match                        The match.
    /// @param[in]  always_serve_remote_aliases  Whether or not remote alias
    ///                                          matches should always be
    ///                                          accepted.
    /// @param[in]  alias                        The alias of match.sproutlet
    ///                                          used to obtain the match.
    /// @param[out] tsx_match_locality           The locality of the tsx match.
    ///
    /// @return     True if the match should be rejected, and false otherwise.
    ///
    bool accept_sproutlet_match(SproutletMatch match,
                                bool always_serve_remote_aliases,
                                std::string& alias,
                                SproutletTsxMatchLocality& tsx_match_locality);

    ///
    /// @brief      Finds a SproutletTsx willing to handle a request.
    ///
    /// Initially attempts to match the request against a Sproutlet. If a
    /// Sproutlet is found, we determine whether or not the Sproutlet is
    /// interested in handling the request. If it is, the request is passed to
    /// that sproutlet. If not, we attempt to find another Sproutlet - first by
    /// examining any further route headers on the request, and then using the
    /// next hop URI that may be supplied by the previous Sproutlet.
    ///
    /// The function provides context for the returned SproutletTsx in the form
    /// of its locality, which is used by the clients for updating statistics.
    ///
    /// @param[in]  req                          The request
    /// @param[in]  port                         The port on which the request
    ///                                          was recieved. Used to attempt
    ///                                          port matching. This can be
    ///                                          disabled by passing zero.
    /// @param[out] alias                        The alias of the Sproutlet that
    ///                                          matched the request.
    /// @param[in]  always_serve_remote_aliases  Whether or not to handle
    ///                                          requests for remote aliases
    ///                                          locally in all cases.
    ///
    /// @return     The matched SproutletTsx, or NULL, and the match locality.
    ///             See accept_sproutlet_match for details on tsx match
    ///             localities.
    ///
    SproutletTsxMatch get_sproutlet_tsx(pjsip_tx_data* req,
                                        int port,
                                        std::string& alias,
                                        bool always_serve_remote_aliases);

    /// The root Sproutlet for this transaction.
    SproutletWrapper* _root;

    /// Templated type used to map from upstream Sproutlet/fork to the
    /// downstream Sproutlet or UACTsx.
    template<typename T>
    struct DMap
    {
      typedef std::map<std::pair<SproutletWrapper*, int>, T> type;
      typedef typename std::map<std::pair<SproutletWrapper*, int>, T>::iterator iterator;
    };

    /// Mapping from upstream Sproutlet/fork to downstream Sproutlet.
    DMap<SproutletWrapper*>::type _dmap_sproutlet;

    /// Mapping from upstream Sproutlet/fork to downstream UACTsx.
    DMap<UACTsx*>::type _dmap_uac;

    /// Mapping from downstream Sproutlet or UAC transaction to upstream
    /// Sproutlet/fork.
    typedef std::map<void*, std::pair<SproutletWrapper*, int> > UMap;
    UMap _umap;

    /// Queue of pending requests to be scheduled.
    typedef struct
    {
      pjsip_tx_data* req;
      std::pair<SproutletWrapper*, int> upstream;
      int allowed_host_state;
      int sproutlet_depth;
      std::string upstream_network_func;
    } PendingRequest;
    std::queue<PendingRequest> _pending_req_q;

    /// Parent proxy object
    SproutletProxy* _sproutlet_proxy;

    /// This set holds all the timers created by sproutlet tsxs that are
    /// children of this UASTsx. They are only freed when the UASTsx is freed
    /// (they are not freed when a timer pops or is cancelled for example).
    /// This prevents race conditions (such as a double free caused by one
    /// thread popping a timer and another thread cancelling it).
    std::set<pj_timer_entry*> _timers;

    /// This set holds all the timers created by sproutlet tsx that are
    /// children of this UASTsx that have not popped or been cancelled yet.
    /// The UASTsx will persist while there are pending timers.
    std::set<pj_timer_entry*> _pending_timers;

    /// Count of the number of UASTsx objects currently active. Used for
    /// debugging purposes.
    static std::atomic_int _num_instances;

    friend class SproutletWrapper;
  };

  pjsip_sip_uri* _root_uri;
  std::map<std::string, pjsip_sip_uri*> _root_uris;

  std::unordered_set<std::string> _host_local_aliases;
  std::unordered_set<std::string> _host_remote_aliases;
  bool _always_serve_remote_aliases;

  std::map<std::string, Sproutlet*> _services;

  std::map<int, Sproutlet*> _ports;

  std::list<Sproutlet*> _sproutlets;

  static const pj_str_t STR_SERVICE;

  SNMP::CounterTable* _route_to_remote_alias_tbl;
  SNMP::CounterTable* _accept_for_remote_alias_tbl;

  const int _max_sproutlet_depth;

  friend class UASTsx;
  friend class SproutletWrapper;
};


class SproutletWrapper : public SproutletTsxHelper
{
public:
  static constexpr const char* EXTERNAL_NETWORK_FUNCTION = "EXTERNAL";

  /// Constructor
  SproutletWrapper(SproutletProxy* proxy,
                   SproutletProxy::UASTsx* proxy_tsx,
                   Sproutlet* sproutlet,
                   SproutletTsx* sproutlet_tsx,
                   const std::string& sproutlet_alias,
                   pjsip_tx_data* req,
                   pjsip_transport* original_transport,
                   const std::string& upstream_network_func,
                   int depth,
                   SAS::TrailId trail_id);

  /// Virtual destructor.
  virtual ~SproutletWrapper();

  const std::string& service_name() const;

  /// This implementation has concrete implementations for all of the virtual
  /// functions from SproutletTsxHelper.  See there for function comments for
  /// the following.
  void add_to_dialog(const std::string& dialog_id="");
  pjsip_msg* original_request();
  void copy_original_transport(pjsip_msg*);
  const char* msg_info(pjsip_msg*);
  const pjsip_route_hdr* route_hdr() const;
  const std::string& dialog_id() const;
  pjsip_msg* create_request();
  pjsip_msg* clone_request(pjsip_msg* req);
  pjsip_msg* clone_msg(pjsip_msg* msg);
  pjsip_msg* create_response(pjsip_msg* req,
                             pjsip_status_code status_code,
                             const std::string& status_text="");
  int send_request(pjsip_msg*& req, int allowed_host_state);
  void send_response(pjsip_msg*& rsp);
  void cancel_fork(int fork_id, int st_code = 0, std::string reason = "");
  void cancel_pending_forks(int st_code = 0, std::string reason = "");
  void mark_pending_forks_as_abandoned();
  const ForkState& fork_state(int fork_id);
  void free_msg(pjsip_msg*& msg);
  pj_pool_t* get_pool(const pjsip_msg* msg);
  bool schedule_timer(void* context, TimerID& id, int duration);
  void cancel_timer(TimerID id);
  bool timer_running(TimerID id);
  SAS::TrailId trail() const;
  bool is_uri_reflexive(const pjsip_uri*) const;
  pjsip_sip_uri* get_reflexive_uri(pj_pool_t*) const;
  pjsip_sip_uri* get_routing_uri(const pjsip_msg* req) const;
  pjsip_sip_uri* next_hop_uri(const std::string& service,
                              const pjsip_sip_uri* base_uri,
                              pj_pool_t* pool) const;
  std::string get_local_hostname(const pjsip_sip_uri* uri) const;
  bool is_network_func_boundary() const;
  bool is_internal_network_func_boundary() const;
  int get_depth() const { return _depth; };
  const std::string& get_network_function() const { return _this_network_func; };

private:
  void rx_request(pjsip_tx_data* req,
                  int allowed_host_state=BaseResolver::ALL_LISTS);
  void rx_response(pjsip_tx_data* rsp,
                   int fork_id,
                   ForkErrorState error_state=ForkErrorState::NONE);
  void rx_cancel(pjsip_tx_data* cancel, const std::string& reason);
  void rx_error(int status_code, const std::string& reason);
  void rx_fork_error(ForkErrorState fork_error, int fork_id);
  void on_timer_pop(TimerID id, void* context);
  void register_tdata(pjsip_tx_data* tdata);
  void deregister_tdata(pjsip_tx_data* tdata);

  void process_actions(bool complete_after_actions);
  void aggregate_response(pjsip_tx_data* rsp);
  int count_pending_responses();
  int count_pending_actionable_responses();
  void tx_request(SproutletProxy::SendRequest req, int fork_id);
  void tx_response(pjsip_tx_data* rsp);
  void tx_cancel(int fork_id);
  int compare_sip_sc(int sc1, int sc2);
  bool is_uri_local(const pjsip_uri*) const;
  void log_inter_sproutlet(pjsip_tx_data* tdata, bool downstream);
  ForkErrorState get_error_state() const;

  SproutletProxy* _proxy;

  SproutletProxy::UASTsx* _proxy_tsx;

  Sproutlet* _sproutlet;

  SproutletTsx* _sproutlet_tsx;

  std::string _service_name;
  std::string _service_host;

  /// Identifier for this SproutletTsx instance - currently a concatenation
  /// of the service name and the address of the object.
  std::string _id;

  /// Reference to the original request. This can been modified by the Sproutlet
  /// Proxy depending on where it sends this message. A clone of this is passed
  /// to the root Sproutlet.
  pjsip_tx_data* _req;
  SNMP::SIPRequestTypes _req_type;

  // Immutable reference to the transport used by the original request.
  pjsip_transport* _original_transport;

  // The name of the Network Function of this Sproutlet.
  std::string _this_network_func;

  // The name of the Network Function of the upstream Sproutlet (if any).
  // This is used to detect transitions between Network Functions, so that we
  // can perform SIP-entity-level operations like sending 100 Trying responses
  // and decrementing the Max-Forwards counter.
  std::string _upstream_network_func;

  // The depth of this wrapper in the transaction tree.  Used to detect loops.
  int _depth;

  typedef std::unordered_map<const pjsip_msg*, pjsip_tx_data*> Packets;
  Packets _packets;

  typedef std::map<int, SproutletProxy::SendRequest> Requests;
  Requests _send_requests;

  typedef std::list<pjsip_tx_data*> Responses;
  Responses _send_responses;

  int _pending_sends;
  pjsip_tx_data* _best_rsp;

  bool _complete;

  // All the actions are performed within SproutletWrapper::process_actions,
  // including deleting the SproutletWrapper itself.  However, process_actions
  // can be re-entered - it sends messages, which can fail and call back into
  // the SproutletWrapper synchronously.  This counter counts how many times
  // the method has currently been entered - if it is non-zero, the
  // SproutletWrapper must not be destroyed.
  int _process_actions_entered;

  /// Vector keeping track of the status of each fork.  The state field can
  /// only ever take a subset of the values defined by PJSIP - NULL, CALLING,
  /// PROCEEDING and TERMINATED.
  typedef struct
  {
    ForkState state;
    pjsip_tx_data* req;
    bool pending_cancel;
    int cancel_st_code;
    std::string cancel_reason;
    bool pending_response;
    bool abandoned;
  } ForkStatus;
  std::vector<ForkStatus> _forks;

  /// Set keeping track of pending timers for this SproutletWrapper.  The
  /// SproutletWrapper (and the SproutletTsx it wraps) won't be deleted
  /// until all these timers have popped or been cancelled.
  std::set<TimerID> _pending_timers;

  // The allowed host state for outbound requests from the sproutlet wrapped by
  // this wrapper.  If there are no addresses of the appropriate state (e.g.
  // whitelisted), then a 503 response will be internally generated, and the
  // error state will be set to indicate that there were no matching addresses.
  int _allowed_host_state;

  SAS::TrailId _trail_id;

  friend class SproutletProxy::UASTsx;
};

#endif
