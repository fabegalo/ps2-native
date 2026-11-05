#pragma once
#include "ps2native/checkpoint.hpp"
#include "ps2native/io.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace ps2native::cli {
struct CheckpointOptions {
    std::filesystem::path resume,output;
    CheckpointContext context;
    std::uint64_t interval{100'000'000};
    bool interval_given{};
    // Execution only: excludes checkpoint load/save and host report export.
    // Two clock reads per run/chunk, never per guest instruction.
    mutable double execution_seconds{};
    template<class C> bool parse(const std::string& flag,const C* native) {
        const auto path=std::filesystem::path(native).native();
        if(flag.starts_with("--resume="))resume=path.substr(9);
        else if(flag.starts_with("--checkpoint-out="))output=path.substr(17);
        else if(flag.starts_with("--checkpoint-scenario="))context.scenario_sha256=flag.substr(22);
        else if(flag=="--checkpoint-allow-runtime-change")context.allow_runtime_change=true;
        else if(flag.starts_with("--checkpoint-every=")) {
            const auto text=flag.substr(19);std::size_t used{};
            if(text.empty() || text.front()=='-')throw std::runtime_error("invalid checkpoint interval");
            interval=std::stoull(text,&used,0);interval_given=true;
            if(used!=text.size() || !interval)throw std::runtime_error("invalid checkpoint interval");
        } else return false;
        return true;
    }
    BootReport run(EEState& state,Memory& memory,const AotModule* module,const BootOptions& options,
                   std::span<const std::filesystem::path> inputs)const {
        execution_seconds=0;
        const auto execute=[&](const BootOptions& chunk) {
            const auto start=std::chrono::steady_clock::now();
            auto report=run_ee_boot(state,memory,module,chunk);
            execution_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            return report;
        };
        if(resume.empty() && output.empty()) {
            if(interval_given || context.allow_runtime_change || !context.scenario_sha256.empty())throw std::runtime_error("checkpoint options require --resume or --checkpoint-out");
            return execute(options);
        }
        if(context.scenario_sha256.empty())throw std::runtime_error("checkpoint requires --checkpoint-scenario=SHA256 of the explicit input/profile/experimental configuration");
        for(const auto& input:inputs)if(!input.empty() && file_exists(input) && !output.empty() && file_exists(output) && files_equivalent(input,output))throw std::runtime_error("checkpoint must not overwrite an input");
        if(!resume.empty()) {
            const auto start=std::chrono::steady_clock::now();const auto info=load_checkpoint(resume,state,memory,context);
            std::cout<<"CHECKPOINT_RESTORED retired="<<info.retired<<" cycles="<<info.cycles<<" pc=0x"<<std::hex<<info.pc<<std::dec
                <<" load_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<'\n';
        }
        if(output.empty())return execute(options);
        auto chunk=options;std::uint64_t retired{},aot{},interpreted{},switches{},frames{};
        std::vector<std::string> trace,tail;
        const auto append=[](auto& destination,auto& source,std::size_t limit){for(auto& entry:source)destination.push_back(std::move(entry));if(limit && destination.size()>limit)destination.erase(destination.begin(),destination.end()-limit);};
        for(;;) {
            chunk.max_instructions=std::min(interval,options.max_instructions-retired);
            auto report=execute(chunk);
            retired+=report.retired;aot+=report.aot_instructions;interpreted+=report.interpreted_instructions;
            switches+=report.engine_switches;frames+=report.frames_presented;
            append(trace,report.trace,options.max_trace_events);append(tail,report.tail,options.tail_instructions);
            if(report.outcome!=BootOutcome::BlockPoint) {
                save_checkpoint(output,state,memory,report,context);
                std::cout<<"CHECKPOINT_SAVED retired="<<state.retired_instructions<<" cycles="<<state.cycles()<<" pc=0x"<<std::hex<<state.pc<<std::dec<<'\n';
            }
            if(report.outcome!=BootOutcome::BudgetExhausted || retired>=options.max_instructions) {
                report.retired=retired;report.aot_instructions=aot;report.interpreted_instructions=interpreted;
                report.engine_switches=switches;report.frames_presented=frames;report.trace=std::move(trace);report.tail=std::move(tail);return report;
            }
            if(!report.retired)throw std::runtime_error("checkpoint runner made no execution progress");
        }
    }
    void print_execution_metrics(const BootReport& report)const {
        std::cout<<"EXECUTION retired="<<report.retired<<" aot="<<report.aot_instructions
            <<" interpreted="<<report.interpreted_instructions
            <<" aot_percent="<<(report.retired ? 100.0*report.aot_instructions/report.retired : 0.0)
            <<" execution_ms="<<execution_seconds*1000.0
            <<" instructions_per_second="<<(execution_seconds>0 ? report.retired/execution_seconds : 0.0)<<'\n';
    }
};
} // namespace ps2native::cli
