//***************************************************************************
//* Copyright (c) 2015 Saint Petersburg State University
//* Copyright (c) 2011-2014 Saint Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//***************************************************************************

#include "pe_config_struct.hpp"
#include "config_common.hpp"

namespace path_extend {

void load(output_broken_scaffolds& obs, boost::property_tree::ptree const& pt, std::string const& key, bool complete) {
  if (complete || pt.find(key) != pt.not_found()) {
    std::string ep = pt.get<std::string>(key);
    obs = pe_config::output_broken_scaffolds_id(ep);
  }
}

void load(scaffolding_mode &sm, boost::property_tree::ptree const& pt, std::string const& key, bool complete) {
    if (complete || pt.find(key) != pt.not_found()) {
        std::string ep = pt.get<std::string>(key);
        sm = pe_config::scaffolding_mode_id(ep);
    }
}

void load(pe_config::ScaffoldGraphParamsT& sg, boost::property_tree::ptree const& pt, bool /*complete*/) {
    using config_common::load;
    load(sg.construct,          pt, "construct"         );
    load(sg.output,             pt, "output"            );
    load(sg.min_read_count,     pt, "min_read_count"    );
    load(sg.graph_connectivity, pt, "graph_connectivity");
    load(sg.max_path_length,    pt, "max_path_length"   );
}

void load(pe_config::OutputParamsT& o, boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;

  load(o.write_overlaped_paths,   pt, "write_overlaped_paths" );
  load(o.write_paths,             pt, "write_paths"           );
}

void load(pe_config::VisualizeParamsT& o, boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;
  load(o.print_overlaped_paths,   pt, "print_overlaped_paths" );
  load(o.print_paths,             pt, "print_paths"           );
}

void load(pe_config::ParamSetT::ExtensionOptionsT& es,
          boost::property_tree::ptree const& pt, bool ) {
    using config_common::load;
    load(es.use_default_single_threshold, pt, "use_default_single_threshold");
    load(es.priority_coeff, pt, "priority_coeff");
    load(es.weight_threshold, pt, "weight_threshold");
    load(es.single_threshold, pt, "single_threshold");
}

void load(pe_config::ParamSetT::LoopRemovalT& lr,
          boost::property_tree::ptree const& pt, bool /*complete*/) {
    using config_common::load;
    load(lr.max_loops, pt, "max_loops");
    load(lr.mp_max_loops, pt, "mp_max_loops");
}

void load(pe_config::ParamSetT::ScaffolderOptionsT& so, boost::property_tree::ptree const& pt, bool /*complete*/)
{
  using config_common::load;
  load(so.on      , pt, "on"      );
  load(so.cutoff      , pt, "cutoff"      );
  load(so.rel_cutoff      , pt, "rel_cutoff"      );
  load(so.sum_threshold      , pt, "sum_threshold"      );

  load(so.cluster_info      , pt, "cluster_info"      );
  load(so.cl_threshold      , pt, "cl_threshold"      );

  load(so.fix_gaps      , pt, "fix_gaps"      );
  load(so.min_gap_score      , pt, "min_gap_score"      );
  load(so.max_must_overlap      , pt, "max_must_overlap"      );
  load(so.max_can_overlap      , pt, "max_can_overlap"      );
  load(so.short_overlap      , pt, "short_overlap"      );
  load(so.artificial_gap      , pt, "artificial_gap"      );
  load(so.use_old_score      , pt, "use_old_score"      );
}

void load(pe_config::ParamSetT& p, boost::property_tree::ptree const& pt, bool /*complete*/) {

  using config_common::load;
  load(p.sm, pt, "scaffolding_mode");
  load(p.normalize_weight, pt,  "normalize_weight");
  load(p.cut_all_overlaps, pt, "cut_all_overlaps");
  load(p.split_edge_length, pt, "split_edge_length");
  load(p.extension_options, pt, "extension_options");
  load(p.mate_pair_options, pt, "mate_pair_options");
  load(p.scaffolder_options, pt, "scaffolder");
    load(p.loop_removal, pt, "loop_removal");
    load(p.remove_overlaps, pt, "remove_overlaps");
}

void load(pe_config::LongReads& p, boost::property_tree::ptree const& pt,
          bool) {
    using config_common::load;
    load(p.filtering, pt, "filtering");
    load(p.weight_priority, pt, "weight_priority");
    load(p.unique_edge_priority, pt, "unique_edge_priority");
}

void load(pe_config::Scaffolding2015& p, boost::property_tree::ptree const& pt,
          bool) {
    using config_common::load;
    load(p.min_unique_length, pt, "min_unique_length");
    load(p.unique_coverage_variation, pt, "unique_coverage_variation");
}

void load(pe_config::AllLongReads& p, boost::property_tree::ptree const& pt,
          bool) {
    using config_common::load;
    load(p.pacbio_reads, pt, "pacbio_reads");
    load(p.single_reads, pt, "single_reads");
    load(p.contigs, pt, "coverage_base_rr");
}

void load(pe_config::MainPEParamsT& p, boost::property_tree::ptree const& pt,
          bool /*complete*/) {
    using config_common::load;
    load(p.debug_output, pt, "debug_output");
    load(p.finalize_paths, pt, "finalize_paths");
    load(p.output, pt, "output");
    load(p.viz, pt, "visualize");
    load(p.param_set, pt, p.name.c_str());
    load(p.obs, pt, "output_broken_scaffolds");

    load(p.long_reads, pt, "long_reads");
    if (!p.debug_output) {
        p.output.DisableAll();
        p.viz.DisableAll();
    }
    load(p.scaffolding2015, pt, "scaffolding2015");
    load(p.scaffold_graph_params, pt, "scaffold_graph");
    p.etc_dir = "path_extend";
}


// main long contigs config load function
void load(pe_config& pe_cfg, boost::property_tree::ptree const& pt, bool /*complete*/) {
  using config_common::load;

  load(pe_cfg.dataset_name           , pt, "dataset"               );
  load(pe_cfg.params                 , pt, "pe_params"             );
}

};

