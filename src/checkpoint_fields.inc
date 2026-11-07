// Explicit modeled state. Raw bindings are validated and reattached separately.
template<class A> static void fields(A& a,Memory& v){
    a(v.data_cache_,v.data_cache_model_,v.data_cache_uncached_hits_,v.cache_config_,v.ram_,v.unknown_words_,v.fpu_relations_,v.fpu_origin_);
    if(a.legacy_scalar_flags) {
        if constexpr(A::reading) {
            // Old runtimes could not select the scalar experiment. Its history
            // was not tracked, so forbid enabling it after a legacy restore.
            v.diagnostic_ee_early_digit_carry_=false;v.fpu_arithmetic_started_=true;
        }
    } else a(v.diagnostic_ee_early_digit_carry_,v.fpu_arithmetic_started_);
    a(v.registers_,v.rom_,v.rom1_,v.scratchpad_,v.tlb_,v.tlb_defined_,v.mapping_,v.asid_,v.error_level_,v.privilege_);
}
template<class A> static void fields(A& a,EeRegisters& v){a(v.timers_,v.gs_,v.mch_,v.iop_,v.f500_,v.diagnostic_omit_f510_,v.dev9_147x_,v.sio_setup_,v.sio_output_,v.intc_mask_,v.intc_stat_,v.intc_unknown_,v.vertical_generation_,v.vertical_sample_,v.gif_,v.vif0_,v.vif1_,v.ipu_,v.vu_control_,v.video_encoder_,v.encoder_master_190x_,v.encoder_master_1a_);}
template<class A> static void fields(A& a,Iop& v){a(v.ee_ram_write_generation_,v.ee_dctrl_,v.ee_dstat_,v.ee_sif1_chcr_,v.ee_sif1_madr_,v.ee_sif1_qwc_,v.ee_dma_setup_,v.ee_dma_global_,v.ee_dma_global_unknown_,v.iop_sif1_,v.sif1_fifo_,v.sif1_have_tag_,v.sif1_end_,v.sif1_tag_irq_,v.ee_sif1_chain_end_,v.sif1_words_,v.sif1_metrics_,v.iop_sif0_,v.iop_sif0_written_,v.sif0_fifo_,v.sif0_source_words_,v.sif0_source_tag_,v.sif0_source_end_,v.sif0_source_irq_,v.sif0_dest_end_,v.sif0_last_qword_,v.sif0_metrics_,v.diagnostic_sio2_reset_completion_,v.sio2_irq_output_,v.sio2_dma_,v.diagnostic_sio2_dma_cycles_,v.sio2_dma_next_,v.sio2_dma_turn_,v.rom_,v.rom1_,v.ram_,v.ram_known_,v.gpr_,v.gpr_defined_,v.gpr_origin_,v.gpr_known_,v.hi_,v.lo_,v.pc_,v.branch_target_,v.in_delay_slot_,v.pending_load_,v.write_,v.undefined_load_,v.undefined_known_,v.gpr_sio2_epoch_,v.sio2_control_epoch_,v.undefined_sio2_epoch_,v.sio2_remote_epoch_,v.sio2_remote_bypass_,v.diagnostic_sio2_port_timing_,v.sio2_digital_pads_,v.sio2_response_status_,v.sio2_input_,v.sio2_output_,v.sio2_pending_reply_,v.diagnostic_sio2_unplugged_ports_,v.diagnostic_sio2_infrared_absent_,v.sio2_complete_at_,v.diagnostic_sio2_timeout_cycles_,v.sio2_reply_bytes_,v.sio2_interrupt_enable_,v.sio2_diagnostic_control_,v.sio2_completion_flag_,v.sio2_response_valid_,v.cop0_,v.latches_,v.s_params_,v.s_command_,v.boot_certify_request_,v.dvd_playback_forbid_requested_,v.diagnostic_forbid_dvdp_error_clear_,v.diagnostic_wakeup_alarm_disabled_,v.power_event_,v.s_results_,v.config_,v.timers_,v.timers_running_,v.hblank_timer_mask_,v.hblank_clock_,v.hblank_held_,v.hblank_reason_,v.next_hblank_ee_,v.next_hblank_iop_,v.diagnostic_dev9_controls_,v.dev9_initial_power_ready_,v.dev9_controls_,v.diagnostic_dev9_settle_cycles_,v.dev9_power_ready_at_,v.dev9_power_,v.speed_irq_mask_,v.dev9_irq_acknowledged_,v.diagnostic_dev9_irq_disconnected_,v.speed_eeprom_,v.speed_gpio_dir_,v.speed_gpio_data_,v.speed_gpio_clock_at_,v.istat_,v.sio2_irq_acknowledged_,v.dma_irq_output_,v.cdvd_irq_,v.ee_dma_suspended_,v.cdvd_ncommand_,v.cdvd_complete_at_,v.cdvd_type_,v.cdvd_status_,v.cdvd_sticky_,v.diagnostic_cav_nominal_timing_,v.diagnostic_spu2_quiescent_,v.diagnostic_spu2_7ca_latch_,v.diagnostic_spu2_reserved_latches_,v.spu2_control_,v.spu2_symbolic_voices_,v.spu2_keys_,v.spu2_endx_,v.spu2_release_upper_,v.spu2_release_pending_,v.diagnostic_spu2_effect_end_,v.diagnostic_spu2_effect_start_,v.diagnostic_spu2_effect_address_reserved_zero_,v.diagnostic_spu2_inverted_effect_area_inactive_,v.spu2_effect_area_dirty_,v.spu2_next_sample_,v.spu2_last_sample_,v.spu2_sample_count_,v.spu2_clock_active_,v.spu2_transfer_,v.spu2_ram_,v.spu2_ram_written_,v.diagnostic_spu2_pio_cycles_,v.spu2_adma_,v.diagnostic_spu2_adma_cycles_,v.diagnostic_spu2_dma_wrap_zero_,v.diagnostic_spu2_admas_control_latch_,v.spu2_adma_bus_available_,v.spu2_adma_last_grant_,v.graphics_dma_,v.cdvd_howto_,v.cdvd_params_,v.cdvd_lba_,v.cdvd_sectors_,v.cdvd_sector_cycles_,v.cdvd_sector_at_,v.cdvd_sector_,v.cdvd_sector_offset_,v.sif_mscom_,v.sif_smcom_,v.sif_msflg_,v.sif_smflg_,v.diagnostic_sbus_latches_,v.diagnostic_sif_ready_,v.sbus_control_write_,v.sbus_bd6_write_,v.video_generation_,v.video_,v.next_vblank_start_,v.next_vblank_end_,v.ilink_bus_reset_,v.cycle_,v.stop_,v.stop_cycle_,v.events_);if(!a.legacy_spu2){std::uint64_t tag=0x314D435032555053ull;a(tag,v.spu2_voices_,v.spu2_irq_pending_);if(tag!=0x314D435032555053ull)throw std::runtime_error("checkpoint SPU2 synthesis tag differs");}else if constexpr(A::reading){v.spu2_voices_={};v.spu2_irq_pending_={};}if(!a.legacy_from_spr){std::uint64_t tag=0x31414D445250535Full;a(tag,v.spr_dma_cycle_);if(tag!=0x31414D445250535Full)throw std::runtime_error("checkpoint fromSPR tag differs");}else if constexpr(A::reading){if(v.ee_dma_setup_[8][0].value_or(0u)&0x100u)throw std::runtime_error("legacy checkpoint cannot contain active fromSPR DMA");v.spr_dma_cycle_=v.graphics_dma_[0].cycle;}if(!a.legacy_voice_readback){std::uint64_t tag=0x314158414E325053ull;a(tag,v.diagnostic_spu2_voice_address_reserved_zero_);if(tag!=0x314158414E325053ull)throw std::runtime_error("checkpoint SPU2 voice readback tag differs");}else if constexpr(A::reading){v.diagnostic_spu2_voice_address_reserved_zero_=false;}if(!a.legacy_graphics_bus){std::uint64_t tag=0x3130535542464947ull;a(tag,v.graphics_dma_turn_);if(tag!=0x3130535542464947ull || v.graphics_dma_turn_<1u || v.graphics_dma_turn_>2u)throw std::runtime_error("checkpoint graphics bus extension differs");}else if constexpr(A::reading){v.graphics_dma_turn_=1;}if constexpr(A::reading)v.spu2_sample_sink_={};}
template<class A> static void fields(A& a,GsRegisters::Vertex& v){a(v.x,v.y,v.u,v.v,v.z,v.rgbaq,v.st,v.uv,v.rgbaq_unknown);}
template<class A> static void fields(A& a,GsRegisters& v){a(v.mode_,v.diagnostic_reset_syncv_,v.diagnostic_reset_field_,v.field_,v.diagnostic_reset_scanmask_,v.diagnostic_reset_texture_cache_invalid_,v.diagnostic_texture_precision_,v.csr_written_,v.general_,v.general_unknown_,v.display_,v.vram_,v.first_vertex_,v.triangle_vertices_,v.triangle_vertices_count_,v.clut_,v.clut_base_,v.sampled_pages_,v.dirty_texture_pages_,v.transfer_generation_,v.texture_flush_generation_,v.transfer_,v.imr_,v.events_,v.unknown_events_,v.event_cycle_,v.irq_pending_,v.irq_unknown_,v.finish_pending_,v.siglblid_,v.siglblid_known_,v.line_,v.generation_);
    if(a.legacy_vertex_st) {
        if constexpr(A::reading) {
            if(v.first_vertex_)v.first_vertex_->st_unknown=v.first_vertex_->st?0xFFFFFF00FFFFFF00ull:0;
            for(auto& vertex:v.triangle_vertices_)vertex.st_unknown=vertex.st?0xFFFFFF00FFFFFF00ull:0;
        }
    } else {
        std::array<char,8> tag{'G','S','S','T','U','N','K','1'};const auto expected=tag;a(tag);
        if(tag!=expected)throw std::runtime_error("checkpoint GS ST uncertainty extension is invalid");
        std::uint64_t first=v.first_vertex_?v.first_vertex_->st_unknown:0;
        a(first,v.triangle_vertices_[0].st_unknown,v.triangle_vertices_[1].st_unknown);
        if constexpr(A::reading) {
            if(v.first_vertex_)v.first_vertex_->st_unknown=first;
            else if(first)throw std::runtime_error("checkpoint GS absent vertex has ST uncertainty");
        }
    }
}
template<class A> static void fields(A& a,GsVram& v){a(v.words_,v.known_,v.revision_,v.pixel_writes_);}
template<class A> static void fields(A& a,Gif& v){a(v.path_,v.ctrl_,v.mode_,v.vif_path3_mask_,v.path1_requested_,v.in_packet_,v.end_of_packet_,v.loops_left_,v.nreg_,v.regs_,v.reg_index_,v.written_,v.format_,v.q_,v.packets_);}
template<class A> static void fields(A& a,Vif::Input& v){a(v.words,v.first,v.count);}
template<class A> static void fields(A& a,Vif& v){a(v.unit_,v.direct_left_,v.direct_buffer_,v.direct_words_,v.mpg_left_,v.mpg_address_,v.mpg_low_,v.unpack_left_,v.unpack_address_,v.unpack_index_,v.unpack_cycle_,v.unpack_components_,v.unpack_cl_,v.unpack_wl_,v.unpack_mode_,v.unpack_mask_,v.unpack_data_,v.error_mask_,v.mark_,v.code_,v.pending_,v.pending_index_,v.mark_detected_,v.dbf_,v.fifo_,v.input_,v.input_index_,v.input_count_,v.waiting_,v.padding_,v.gif_waiting_,v.programmed_);
    if(!a.legacy_vif_payload) {
        std::uint64_t tag=0x31304B4E55464956ull,count=v.fifo_.size();
        a(tag,count);
        if(tag!=0x31304B4E55464956ull || count!=v.fifo_.size())throw std::runtime_error("checkpoint VIF uncertainty extension differs");
        a(v.input_.unknown,v.unpack_unknown_);for(auto& input:v.fifo_)a(input.unknown);
    }else if constexpr(A::reading){v.input_.unknown={};v.unpack_unknown_={};for(auto& input:v.fifo_)input.unknown={};}
    if(!a.legacy_transfer_bounds) {
        std::uint64_t tag=0x3130444E42464956ull,count=v.fifo_.size();a(tag,count);
        if(tag!=0x3130444E42464956ull || count!=v.fifo_.size())throw std::runtime_error("checkpoint VIF numeric certificate extension differs");
        a(v.input_.bounds,v.unpack_bounds_);for(auto& input:v.fifo_)a(input.bounds);
    }else if constexpr(A::reading){v.input_.bounds={};v.unpack_bounds_={};for(auto& input:v.fifo_)input.bounds={};}
    if(!a.legacy_vif_interrupt) {
        std::uint64_t tag=0x3130515249464956ull;a(tag);
        if(tag!=0x3130515249464956ull)throw std::runtime_error("checkpoint VIF interrupt extension differs");
        a(v.interrupt_stall_,v.interrupt_edge_,v.command_interrupt_);
    }else if constexpr(A::reading){v.interrupt_stall_=v.interrupt_edge_=v.command_interrupt_=false;}
}
template<class A> static void fields(A& a,VuControl::MicroVectorWrite& v){a(v.index,v.mask,v.words,v.ready);}
template<class A> static void fields(A& a,VuControl& v){a(v.micro_,v.path1_,v.queued_path1_,v.pending_q_,v.pending_q1_,v.registers_,v.diagnostic_power_two_carry_,v.unknown_,v.vu1_integers_,v.vu1_controls_,v.data_,v.vectors_,v.vector_bounds_,v.accumulator_bounds_,v.q_bounds_,v.accumulator_,v.accumulator1_,v.vu1_vectors_,v.micro0_,v.micro1_,v.data1_);
    if(!a.legacy_micro_bounds) {
        std::uint64_t tag=0x3130444E424D5556ull;a(tag);
        if constexpr(A::reading)if(tag!=0x3130444E424D5556ull)throw std::runtime_error("checkpoint VU bounds tag differs");
        a(v.vu1_vector_bounds_);
        for(auto& unit:v.micro_) {
            for(auto& write:unit.vectors)a(write.bounds);
            for(auto& write:unit.stores)a(write.bounds);
        }
    }else if constexpr(A::reading) {
        v.vu1_vector_bounds_={};
        for(auto& unit:v.micro_) {
            for(auto& write:unit.vectors)write.bounds={};
            for(auto& write:unit.stores)write.bounds={};
        }
    }
    if(!a.legacy_transfer_bounds) {
        std::uint64_t tag=0x3130444E42445556ull;a(tag,v.data_bounds_,v.data1_bounds_);
        if(tag!=0x3130444E42445556ull)throw std::runtime_error("checkpoint VU data certificate tag differs");
    }else if constexpr(A::reading){v.data_bounds_.assign(v.data_.size(),std::nullopt);v.data1_bounds_.assign(v.data1_.size(),std::nullopt);}

}

template<class A> static void fields(A& a,IpuRegisters& v){a(v.control_,v.bit_position_,v.input_,v.pending_,v.thresholds_,v.iq_,v.vq_);}
template<class A> static void fields(A& a,MchRegisters& v){a(v.chain_,v.pending_,v.pending_read_,v.ricm_,v.busy_until_,v.write_data_,v.read_data_,v.setr_done_,v.control_,v.control_busy_until_);}
template<class A> static void fields(A& a,RdramChain& v){a(v.devices_);}
template<class A> static void fields(A& a,VideoEncoderBus& v){a(v.pointer_,v.written_,v.assumed_,v.log_);}
template<class A> static void fields(A& a,VideoEncoderMaster& v){a(v.layout_,v.latched_,v.status_,v.enabled_,v.data_,v.send_,v.received_);}
template<class A> static void fields(A& a,Microwire64x16& v){a(v.words_,v.presence_,v.backed_,v.selected_,v.reading_,v.bits_,v.address_,v.bit_,v.command_,v.output_,v.words_read_,v.commands_);}
template<class A> static void fields(A& a,Iop::MechaReplyByte& v){a(v.value,v.known,v.origin);}
template<class A,class T> static void fields(A& a,DiagnosticHistory<T>& v){a(v.data_,v.next_,v.limit_);}
