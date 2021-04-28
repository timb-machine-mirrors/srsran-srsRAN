/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsran/upper/pdcp.h"
#include "srsran/upper/pdcp_entity_nr.h"

namespace srsran {

pdcp::pdcp(srsran::task_sched_handle task_sched_, const char* logname) :
  task_sched(task_sched_), logger(srslog::fetch_basic_logger(logname))
{}

pdcp::~pdcp()
{
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    valid_lcids_cached.clear();
  }
  // destroy all remaining entities
  pdcp_array.clear();
  pdcp_array_mrb.clear();
}

void pdcp::init(srsue::rlc_interface_pdcp* rlc_,
                srsue::rrc_interface_pdcp* rrc_,
                srsue::rrc_interface_pdcp* rrc_nr_,
                srsue::gw_interface_pdcp*  gw_)
{
  init(rlc_, rrc_, gw_);
  rrc_nr = rrc_nr_;
}

void pdcp::init(srsue::rlc_interface_pdcp* rlc_, srsue::rrc_interface_pdcp* rrc_, srsue::gw_interface_pdcp* gw_)
{
  rlc = rlc_;
  rrc = rrc_;
  gw  = gw_;
}

void pdcp::stop() {}

void pdcp::reestablish()
{
  for (auto& lcid_it : pdcp_array) {
    lcid_it.second->reestablish();
  }
}

void pdcp::reestablish(uint32_t lcid)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->reestablish();
  }
}

void pdcp::reset()
{
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    valid_lcids_cached.clear();
  }
  // destroy all bearers
  pdcp_array.clear();
}

/*******************************************************************************
  RRC/GW interface
*******************************************************************************/

// NOTE: Called from separate thread
bool pdcp::is_lcid_enabled(uint32_t lcid)
{
  std::lock_guard<std::mutex> lock(cache_mutex);
  return valid_lcids_cached.count(lcid) > 0;
}

void pdcp::write_sdu(uint32_t lcid, unique_byte_buffer_t sdu, int sn)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->write_sdu(std::move(sdu), sn);
  } else {
    logger.warning("Writing sdu: lcid=%d. Deallocating sdu", lcid);
  }
}

void pdcp::write_sdu_mch(uint32_t lcid, unique_byte_buffer_t sdu)
{
  if (valid_mch_lcid(lcid)) {
    pdcp_array_mrb.at(lcid)->write_sdu(std::move(sdu));
  }
}

void pdcp::add_bearer(uint32_t lcid, pdcp_config_t cfg)
{
  if (not valid_lcid(lcid)) {
    std::unique_ptr<pdcp_entity_base> entity;
    // For now we create an pdcp entity lte for nr due to it's maturity
    if (cfg.rat == srsran::srsran_rat_t::lte) {
      entity.reset(new pdcp_entity_lte{rlc, rrc, gw, task_sched, logger, lcid});
    } else if (cfg.rat == srsran::srsran_rat_t::nr) {
      if (rrc_nr == nullptr) {
        logger.warning("Cannot add PDCP entity - missing rrc_nr parent pointer");
        return;
      }
      entity.reset(new pdcp_entity_lte{rlc, rrc_nr, gw, task_sched, logger, lcid});
    }

    if (not entity->configure(cfg)) {
      logger.error("Can not configure PDCP entity");
      return;
    }

    if (not pdcp_array.insert(std::make_pair(lcid, std::move(entity))).second) {
      logger.error("Error inserting PDCP entity in to array.");
      return;
    }
    logger.info(
        "Add %s (lcid=%d, bearer_id=%d, sn_len=%dbits)", rrc->get_rb_name(lcid), lcid, cfg.bearer_id, cfg.sn_len);
    {
      std::lock_guard<std::mutex> lock(cache_mutex);
      valid_lcids_cached.insert(lcid);
    }
  } else {
    logger.info("Bearer %s already configured.", rrc->get_rb_name(lcid));
  }
}

void pdcp::add_bearer_mrb(uint32_t lcid, pdcp_config_t cfg)
{
  if (not valid_mch_lcid(lcid)) {
    std::unique_ptr<pdcp_entity_lte> entity;
    entity.reset(new pdcp_entity_lte{rlc, rrc, gw, task_sched, logger, lcid});
    if (not entity->configure(cfg)) {
      logger.error("Can not configure PDCP entity");
      return;
    }

    if (not pdcp_array_mrb.insert(std::make_pair(lcid, std::move(entity))).second) {
      logger.error("Error inserting PDCP entity in to array.");
      return;
    }
    logger.info(
        "Add %s (lcid=%d, bearer_id=%d, sn_len=%dbits)", rrc->get_rb_name(lcid), lcid, cfg.bearer_id, cfg.sn_len);
  } else {
    logger.warning("Bearer %s already configured. Reconfiguration not supported", rrc->get_rb_name(lcid));
  }
}

void pdcp::del_bearer(uint32_t lcid)
{
  {
    std::lock_guard<std::mutex> lock(cache_mutex);
    valid_lcids_cached.erase(lcid);
  }
  if (valid_lcid(lcid)) {
    pdcp_array.erase(lcid);
    logger.warning("Deleted PDCP bearer %s", rrc->get_rb_name(lcid));
  } else {
    logger.warning("Can't delete bearer %s. Bearer doesn't exist.", rrc->get_rb_name(lcid));
  }
}

void pdcp::change_lcid(uint32_t old_lcid, uint32_t new_lcid)
{
  // make sure old LCID exists and new LCID is still free
  if (valid_lcid(old_lcid) && not valid_lcid(new_lcid)) {
    // insert old PDCP entity into new LCID
    std::lock_guard<std::mutex>       lock(cache_mutex);
    auto                              it          = pdcp_array.find(old_lcid);
    std::unique_ptr<pdcp_entity_base> pdcp_entity = std::move(it->second);
    if (not pdcp_array.insert(std::make_pair(new_lcid, std::move(pdcp_entity))).second) {
      logger.error("Error inserting PDCP entity into array.");
      return;
    }
    // erase from old position
    pdcp_array.erase(it);
    valid_lcids_cached.erase(old_lcid);
    valid_lcids_cached.insert(new_lcid);
    logger.warning("Changed LCID of PDCP bearer from %d to %d", old_lcid, new_lcid);
  } else {
    logger.error("Can't change PDCP of bearer %s from %d to %d. Bearer doesn't exist or new LCID already occupied.",
                 rrc->get_rb_name(old_lcid),
                 old_lcid,
                 new_lcid);
  }
}

void pdcp::config_security(uint32_t lcid, const as_security_config_t& sec_cfg)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->config_security(sec_cfg);
  }
}

void pdcp::config_security_all(const as_security_config_t& sec_cfg)
{
  for (auto& it : pdcp_array) {
    it.second->config_security(sec_cfg);
  }
}

void pdcp::enable_integrity(uint32_t lcid, srsran_direction_t direction)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->enable_integrity(direction);
  }
}

void pdcp::enable_encryption(uint32_t lcid, srsran_direction_t direction)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->enable_encryption(direction);
  }
}

void pdcp::enable_security_timed(uint32_t lcid, srsran_direction_t direction, uint32_t sn)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->enable_security_timed(direction, sn);
  }
}

void pdcp::send_status_report()
{
  for (auto& lcid_it : pdcp_array) {
    lcid_it.second->send_status_report();
  }
}

void pdcp::send_status_report(uint32_t lcid)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->send_status_report();
  }
}

bool pdcp::get_bearer_state(uint32_t lcid, srsran::pdcp_lte_state_t* state)
{
  if (not valid_lcid(lcid)) {
    return false;
  }
  pdcp_array[lcid]->get_bearer_state(state);
  return true;
}

bool pdcp::set_bearer_state(uint32_t lcid, const srsran::pdcp_lte_state_t& state)
{
  if (not valid_lcid(lcid)) {
    return false;
  }
  pdcp_array[lcid]->set_bearer_state(state, true);
  return true;
}

std::map<uint32_t, srsran::unique_byte_buffer_t> pdcp::get_buffered_pdus(uint32_t lcid)
{
  if (not valid_lcid(lcid)) {
    return {};
  }
  return pdcp_array[lcid]->get_buffered_pdus();
}

/*******************************************************************************
  RLC interface
*******************************************************************************/
void pdcp::write_pdu(uint32_t lcid, unique_byte_buffer_t pdu)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->write_pdu(std::move(pdu));
  } else {
    logger.warning("Writing pdu: lcid=%d. Deallocating pdu", lcid);
  }
}

void pdcp::write_pdu_bcch_bch(unique_byte_buffer_t sdu)
{
  rrc->write_pdu_bcch_bch(std::move(sdu));
}

void pdcp::write_pdu_bcch_dlsch(unique_byte_buffer_t sdu)
{
  rrc->write_pdu_bcch_dlsch(std::move(sdu));
}

void pdcp::write_pdu_pcch(unique_byte_buffer_t sdu)
{
  rrc->write_pdu_pcch(std::move(sdu));
}

void pdcp::write_pdu_mch(uint32_t lcid, unique_byte_buffer_t sdu)
{
  if (0 == lcid) {
    rrc->write_pdu_mch(lcid, std::move(sdu));
  } else {
    gw->write_pdu_mch(lcid, std::move(sdu));
  }
}

void pdcp::notify_delivery(uint32_t lcid, const pdcp_sn_vector_t& pdcp_sns)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->notify_delivery(pdcp_sns);
  } else {
    logger.warning("Could not notify delivery: lcid=%d, nof_sn=%ld.", lcid, pdcp_sns.size());
  }
}

void pdcp::notify_failure(uint32_t lcid, const srsran::pdcp_sn_vector_t& pdcp_sns)
{
  if (valid_lcid(lcid)) {
    pdcp_array.at(lcid)->notify_failure(pdcp_sns);
  } else {
    logger.warning("Could not notify failure: lcid=%d, nof_sn=%ld.", lcid, pdcp_sns.size());
  }
}

bool pdcp::valid_lcid(uint32_t lcid)
{
  if (lcid >= SRSRAN_N_RADIO_BEARERS) {
    logger.error("Radio bearer id must be in [0:%d] - %d", SRSRAN_N_RADIO_BEARERS, lcid);
    return false;
  }

  return pdcp_array.find(lcid) != pdcp_array.end();
}

bool pdcp::valid_mch_lcid(uint32_t lcid)
{
  if (lcid >= SRSRAN_N_MCH_LCIDS) {
    logger.error("Radio bearer id must be in [0:%d] - %d", SRSRAN_N_RADIO_BEARERS, lcid);
    return false;
  }

  return pdcp_array_mrb.find(lcid) != pdcp_array_mrb.end();
}

void pdcp::get_metrics(pdcp_metrics_t& m, const uint32_t nof_tti)
{
  std::chrono::duration<double> secs = std::chrono::high_resolution_clock::now() - metrics_tp;

  for (pdcp_map_t::iterator it = pdcp_array.begin(); it != pdcp_array.end(); ++it) {
    pdcp_bearer_metrics_t metrics = it->second->get_metrics();

    // Rx/Tx rate based on real time
    double rx_rate_mbps_real_time = (metrics.num_rx_pdu_bytes * 8 / (double)1e6) / secs.count();
    double tx_rate_mbps_real_time = (metrics.num_tx_pdu_bytes * 8 / (double)1e6) / secs.count();

    // Rx/Tx rate based on number of TTIs
    double rx_rate_mbps = (nof_tti > 0) ? ((metrics.num_rx_pdu_bytes * 8 / (double)1e6) / (nof_tti / 1000.0)) : 0.0;
    double tx_rate_mbps = (nof_tti > 0) ? ((metrics.num_tx_pdu_bytes * 8 / (double)1e6) / (nof_tti / 1000.0)) : 0.0;

    logger.info("lcid=%d, rx_rate_mbps=%4.2f (real=%4.2f), tx_rate_mbps=%4.2f (real=%4.2f)",
                it->first,
                rx_rate_mbps,
                rx_rate_mbps_real_time,
                tx_rate_mbps,
                tx_rate_mbps_real_time);
    m.bearer[it->first] = metrics;
  }

  reset_metrics();
}

void pdcp::reset_metrics()
{
  for (pdcp_map_t::iterator it = pdcp_array.begin(); it != pdcp_array.end(); ++it) {
    it->second->reset_metrics();
  }

  metrics_tp = std::chrono::high_resolution_clock::now();
}

} // namespace srsran
