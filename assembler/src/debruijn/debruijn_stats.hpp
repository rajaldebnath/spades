//***************************************************************************
//* Copyright (c) 2011-2012 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

#pragma once

#include "omni/visualization_utils.hpp"
#include "statistics.hpp"
#include "new_debruijn.hpp"
#include "graph_construction.hpp"
#include "graphio.hpp"
#include "graph_read_correction.hpp"
#include "io/easy_reader.hpp"
#include "omni/edges_position_handler.hpp"
#include "omni/distance_estimation.hpp"
#include "omni/graph_component.hpp"
#include "io/delegating_reader_wrapper.hpp"
#include "omni/pair_info_filters.hpp"
#include "io/easy_reader.hpp"
#include "read/osequencestream.hpp"
#include "io/easy_reader.hpp"
#include <sys/types.h>
#include <sys/stat.h>
#include "copy_file.hpp"
#include <cmath>

namespace debruijn_graph {

template<class Graph>
class GenomeMappingStat: public AbstractStatCounter {
private:
	typedef typename Graph::EdgeId EdgeId;
	const Graph &graph_;
	const EdgeIndex<Graph>& index_;
	Sequence genome_;
	size_t k_;
public:
	GenomeMappingStat(const Graph &graph, const EdgeIndex<Graph> &index,	Sequence genome, size_t k) :
			graph_(graph), index_(index), genome_(genome), k_(k) {
	}

	virtual ~GenomeMappingStat() {
	}

	virtual void Count() {
		INFO("Mapping genome");
		size_t break_number = 0;
		size_t covered_kp1mers = 0;
		size_t fail = 0;
		if (genome_.size() <= k_)
			return;
		runtime_k::RtSeq cur = genome_.start<runtime_k::RtSeq::max_size>(k_);
		cur >>= 0;
		bool breaked = true;
		pair<EdgeId, size_t> cur_position;
		for (size_t cur_nucl = k_; cur_nucl < genome_.size(); cur_nucl++) {
			cur <<= genome_[cur_nucl];
			if (index_.contains(cur)) {
				pair<EdgeId, size_t> next = index_.get(cur);
				if (!breaked
						&& cur_position.second + 1
								< graph_.length(cur_position.first)) {
					if (next.first != cur_position.first
							|| cur_position.second + 1 != next.second) {
						fail++;
					}
				}
				cur_position = next;
				covered_kp1mers++;
				breaked = false;
			} else {
				if (!breaked) {
					breaked = true;
					break_number++;
				}
			}
		}
		INFO("Genome mapped");
		INFO("Genome mapping results:");
		INFO(
				"Covered k+1-mers:" << covered_kp1mers << " of "
						<< (genome_.size() - k_) << " which is "
						<< (100.0 * covered_kp1mers / (genome_.size() - k_))
						<< "%");
		INFO(
				"Covered k+1-mers form " << break_number + 1
						<< " contigious parts");
		INFO("Continuity failtures " << fail);
	}
};

template<class Graph>
class StatCounter: public AbstractStatCounter {
private:
	StatList stats_;
public:
	typedef typename Graph::VertexId VertexId;
	typedef typename Graph::EdgeId EdgeId;

	StatCounter(const Graph& graph, const EdgeIndex<Graph>& index,
	const Sequence& genome, size_t k) {
		SimpleSequenceMapper<Graph> sequence_mapper(graph, index, k + 1);
		Path<EdgeId> path1 = sequence_mapper.MapSequence(Sequence(genome));
		Path<EdgeId> path2 = sequence_mapper.MapSequence(!Sequence(genome));
		stats_.AddStat(new VertexEdgeStat<Graph>(graph));
		stats_.AddStat(new BlackEdgesStat<Graph>(graph, path1, path2));
		stats_.AddStat(new NStat<Graph>(graph, path1, 50));
		stats_.AddStat(new SelfComplementStat<Graph>(graph));
		stats_.AddStat(
				new GenomeMappingStat<Graph>(graph, index,
						Sequence(genome), k));
		stats_.AddStat(new IsolatedEdgesStat<Graph>(graph, path1, path2));
	}

	virtual ~StatCounter() {
		stats_.DeleteStats();
	}

	virtual void Count() {
		stats_.Count();
	}

private:
	DECL_LOGGER("StatCounter")
};

template<class Graph>
void CountStats(const Graph& g, const EdgeIndex<Graph>& index,
const Sequence& genome, size_t k) {
	INFO("Counting stats");
	StatCounter<Graph> stat(g, index, genome, k);
	stat.Count();
	INFO("Stats counted");
}

void CountPairedInfoStats(const Graph &g,
		const PairedInfoIndex<Graph> &paired_index,
		const PairedInfoIndex<Graph> &etalon_paired_index,
		const string &output_folder) {
	PairedInfoIndex<Graph> filtered_index(g);
	PairInfoWeightFilter<Graph>(g, 40).Filter(paired_index, filtered_index);
	INFO("Counting paired info stats");
	EdgePairStat<Graph>(g, paired_index, output_folder).Count();

	//todo remove filtration if launch on etalon info is ok
	UniquePathStat<Graph>(g, filtered_index, *cfg::get().ds.IS,	*cfg::get().ds.RL, 0.1 * (*cfg::get().ds.IS)).Count();
	UniqueDistanceStat<Graph>(etalon_paired_index).Count();
	INFO("Paired info stats counted");
}

// leave only those pairs, which edges have no path in the graph between them
void FilterIndexWithExistingPaths(paired_info_index& scaf_clustered_index, const paired_info_index& index, const conj_graph_pack &gp, const GraphDistanceFinder<Graph>& dist_finder)
{
    for (auto it = index.begin(); it != index.end(); ++it) {
        const vector<omnigraph::PairInfo<EdgeId> >& pair_info = *it;
    	EdgeId e1 = pair_info[0].first;
    	EdgeId e2 = pair_info[0].second;
    	const vector<size_t>& dists = dist_finder.GetGraphDistancesLengths(e1, e2);
        if (dists.size() == 0) {
        	for (size_t i = 0; i < pair_info.size(); ++i) {
                 if (math::gr(pair_info[i].d, 0.))
                     scaf_clustered_index.AddPairInfo(pair_info[i]);
            }
        } 
    }
}

void FillAndCorrectEtalonPairedInfo(
		paired_info_index &corrected_etalon_index, const conj_graph_pack &gp,
		const paired_info_index &paired_index, size_t insert_size,
		size_t read_length, size_t delta,
		bool save_etalon_info_history = false) {
	INFO("Filling etalon paired index");
	paired_info_index etalon_paired_index(gp.g);
    bool successful_load = false;
    if (cfg::get().entry_point >= ws_distance_estimation) {
        string p = path::append_path(cfg::get().load_from, "../etalon");
        if (!path::is_regular_file(p + ".prd")) {
            DEBUG("file " << p + ".prd" << " does not exist");
        }
        else {
            INFO("Loading etalon pair info from the previous run...");
            Graph& graph = const_cast<Graph&>(gp.g);
            IdTrackHandler<Graph>& int_ids = const_cast<IdTrackHandler<Graph>& >(gp.int_ids);
            ScannerTraits<Graph>::Scanner scanner(graph, int_ids);
            scanner.loadPaired(p, etalon_paired_index);
            path::files_t files;
            files.push_back(p);
            copy_files_by_prefix(files, cfg::get().output_dir);
            successful_load = true;
        }
    }
    if (!successful_load) 
	    FillEtalonPairedIndex(etalon_paired_index, gp.g,
			gp.index, gp.kmer_mapper, insert_size, read_length, 4 * delta,
			gp.genome, gp.k_value);
	INFO("Etalon paired index filled");

	INFO("Correction of etalon paired info has been started");

	set<pair<Graph::EdgeId, Graph::EdgeId>> setEdgePairs;
	for (auto iter = paired_index.begin(); iter != paired_index.end(); ++iter)
		setEdgePairs.insert(
				make_pair((*iter)[0].first, (*iter)[0].second));

	INFO("Filtering etalon info");
	//leave only info between edges both present in paired_index
	paired_info_index filtered_etalon_index(gp.g);
	for (auto iter = etalon_paired_index.begin();
			iter != etalon_paired_index.end(); ++iter) {
		const vector<omnigraph::PairInfo<EdgeId> >& pair_info = *iter;
		if (setEdgePairs.count(
				make_pair(pair_info[0].first, pair_info[0].second)) > 0)
			for (auto point = pair_info.begin(); point != pair_info.end();
					++point)
				filtered_etalon_index.AddPairInfo(*point);
	}

	INFO("Pushing etalon info through estimator");
	GraphDistanceFinder<Graph> dist_finder(gp.g, insert_size, read_length,
			delta);
	DistanceEstimator<Graph> estimator(gp.g, filtered_etalon_index, dist_finder,
			0, 4);
	estimator.Estimate(corrected_etalon_index);
	if (save_etalon_info_history) {
		INFO("Saving etalon paired info indices on different stages");
		ConjugateDataPrinter<Graph> data_printer(gp.g, gp.int_ids);
		data_printer.savePaired(cfg::get().output_dir + "etalon",
				etalon_paired_index);
		data_printer.savePaired(
				cfg::get().output_dir + "etalon_filtered_by_index",
				filtered_etalon_index);
		data_printer.savePaired(
				cfg::get().output_dir + "etalon_corrected_by_graph",
				corrected_etalon_index);
		INFO("Everything is saved");

        if (cfg::get().paired_info_scaffolder) {
	        GraphDistanceFinder<Graph> dist_finder(gp.g, insert_size, read_length, delta);
        	INFO("Saving paired information statistics for a scaffolding");
            paired_info_index scaf_etalon_index(gp.g);
            FilterIndexWithExistingPaths(scaf_etalon_index, filtered_etalon_index, gp, dist_finder);
			data_printer.savePaired(
					cfg::get().output_dir + "scaf_etalon",
					scaf_etalon_index);
        }
		
        INFO("Everything saved");
	}
	INFO("Correction finished");
}

template<class Graph>
void GetAllDistances(const PairedInfoIndex<Graph>& paired_index,
		PairedInfoIndex<Graph>& result,
		const GraphDistanceFinder<Graph>& dist_finder) {
	for (auto iter = paired_index.begin(); iter != paired_index.end(); ++iter) {
		const vector<PairInfo<EdgeId> >& data = *iter;
		EdgeId first = data[0].first;
		EdgeId second = data[0].second;
		const vector<size_t>& forward = dist_finder.GetGraphDistancesLengths(first, second);
		for (size_t i = 0; i < forward.size(); ++i)
			result.AddPairInfo(
					PairInfo<EdgeId> (data[0].first, data[0].second, forward[i], -10, 0.0),
					false);
	}
}

template<class Graph>
void GetAllDistances(const Graph& g, const PairedInfoIndex<Graph>& paired_index, const PairedInfoIndex<Graph>& clustered_index, const IdTrackHandler<Graph>& int_ids,
		PairedInfoIndex<Graph>& result,
		const GraphDistanceFinder<Graph>& dist_finder) {
	for (auto iter = paired_index.begin(); iter != paired_index.end(); ++iter) {
		const vector<PairInfo<EdgeId> >& data = *iter;
		EdgeId first = data[0].first;
		EdgeId second = data[0].second;
		const vector<vector<EdgeId> >& raw_paths = dist_finder.GetGraphDistances(first, second);
        // adding first edge to every path
        vector<vector<EdgeId> > paths;
        for (size_t i = 0; i < raw_paths.size(); ++i) {
            vector<EdgeId> path;

            path.push_back(first);
            
            for (size_t j = 0; j < raw_paths[i].size(); ++j) {
                path.push_back(raw_paths[i][j]);
            }

            path.push_back(second);

            paths.push_back(path);
        }

        vector<size_t> path_lengths;
        vector<double> path_weights;
        for (size_t i = 0; i < paths.size(); ++i) {
            size_t len_total = 0 ;
            double weight_total = 0.;
            for (size_t j = 0; j < paths[i].size(); ++j) { 
                len_total += g.length(paths[i][j]);
                size_t cur_length = 0;
                for (size_t l = j + 1; l < paths[i].size(); ++l) {
                    cur_length += g.length(paths[i][l - 1]);
                    const vector<PairInfo<EdgeId> >& infos = clustered_index.GetEdgePairInfo(paths[i][j], paths[i][l]);
                    for (auto iterator = infos.begin(); iterator != infos.end(); ++iterator) {
                        PairInfo<EdgeId> info = *iterator;
                        if (info.d == cur_length) {
                            weight_total += info.weight;
                            break;
                        }
                    }
                }
            }
            path_lengths.push_back(len_total - g.length(second));
            path_weights.push_back(weight_total);
        }

        for (size_t i = 0; i < paths.size(); ++i) {
            cout << int_ids.ReturnIntId(first) << "(" << g.length(first) << ") "
                 << int_ids.ReturnIntId(second) << "(" << g.length(second) << ") : " << (i + 1) << "-th path (" << path_lengths[i] << ", " << path_weights[i] << ")   :::   ";
            
            for (size_t j = 0; j < paths[i].size(); ++j) {
                cout << int_ids.ReturnIntId(paths[i][j]) << "(" << g.length(paths[i][j]) << ") ";       
            }

            cout << endl;
		}
    }
}

template<class Graph>
void CountAndSaveAllPaths(const Graph& g, const IdTrackHandler<Graph>& int_ids,
		const PairedInfoIndex<Graph>& paired_index, const PairedInfoIndex<Graph>& clustered_index) {
	paired_info_index all_paths(g);
    GetAllDistances<Graph>(
            paired_index,
            all_paths,
            GraphDistanceFinder<Graph>(g, *cfg::get().ds.IS, *cfg::get().ds.RL,
            size_t(*cfg::get().ds.is_var)));

	string dir_name = cfg::get().output_dir + "estimation_qual/";
	make_dir(dir_name);

	typename PrinterTraits<Graph>::Printer printer(g, int_ids);
    printer.savePaired(dir_name + "paths", all_paths);

    //paired_info_index all_paths_2(g);
    //GetAllDistances<Graph>(g, 
            //paired_index, clustered_index, 
            //int_ids,
            //all_paths_2,
            //GraphDistanceFinder<Graph>(g, *cfg::get().ds.IS, *cfg::get().ds.RL,
            //size_t(*cfg::get().ds.is_var)));
	//printer.savePaired(dir_name + "paths_all", all_paths_2);
}


void CountClusteredPairedInfoStats(const conj_graph_pack &gp,
		const PairedInfoIndex<Graph> &paired_index,
		const PairedInfoIndex<Graph> &clustered_index) {

	paired_info_index etalon_paired_index(gp.g);

    FillAndCorrectEtalonPairedInfo(etalon_paired_index, gp, paired_index,
			*cfg::get().ds.IS, *cfg::get().ds.RL, *cfg::get().ds.is_var, true);

	INFO("Counting clustered info stats");
	EdgeQuality<Graph> edge_qual(gp.g, gp.index, gp.kmer_mapper, gp.genome);
	EstimationQualityStat<Graph> estimation_stat(gp.g, gp.int_ids, edge_qual,
			paired_index, clustered_index, etalon_paired_index);
	estimation_stat.Count();
	estimation_stat.SaveStats(cfg::get().output_dir + "estimation_qual/");

	CountAndSaveAllPaths(gp.g, gp.int_ids, paired_index, clustered_index);

	INFO("Counting overall cluster stat")
	ClusterStat<Graph>(clustered_index).Count();
	INFO("Overall cluster stat")

    if (cfg::get().paired_info_scaffolder) {
		ConjugateDataPrinter<Graph> data_printer(gp.g, gp.int_ids);
        INFO("Generating the statistics of pair info for scaffolding");
        GraphDistanceFinder<Graph> dist_finder(gp.g, *cfg::get().ds.IS, *cfg::get().ds.RL, *cfg::get().ds.is_var);
        paired_info_index scaf_clustered_index(gp.g);
        FilterIndexWithExistingPaths(scaf_clustered_index, clustered_index, gp, dist_finder);
        data_printer.savePaired(
                cfg::get().output_dir + "scaf_clustered",
                scaf_clustered_index);
    }
	//	PairedInfoIndex<Graph> etalon_clustered_index;
	//	DistanceEstimator<Graph> estimator(g, etalon_paired_index, insert_size,
	//			max_read_length, cfg::get().de.delta,
	//			cfg::get().de.linkage_distance, cfg::get().de.max_distance);
	//	estimator.Estimate(etalon_clustered_index);

	//	PairedInfoIndex<Graph> filtered_clustered_index(g);
	//	PairInfoFilter<Graph> (g, 1000.).Filter(
	//			clustered_index/*etalon_clustered_index*/, filtered_clustered_index);
	INFO("Counting mate-pair transformation stat");
	MatePairTransformStat<Graph>(gp.g, /*filtered_*/clustered_index).Count();
	INFO("Mate-pair transformation stat counted");
	INFO("Clustered info stats counted");
}

void WriteToDotFile(const Graph &g,
		const omnigraph::GraphLabeler<Graph>& labeler, const string& file_name,
		string graph_name, Path<EdgeId> path1/* = Path<EdgeId> ()*/,
		Path<EdgeId> path2/* = Path<EdgeId> ()*/) {
	INFO("Writing graph '" << graph_name << "' to file " << file_name);
	omnigraph::WritePaired(g, labeler, file_name, graph_name, path1, path2);
	INFO("Graph '" << graph_name << "' written to file " << file_name);
}

void DetailedWriteToDot(const Graph &g,
		const omnigraph::GraphLabeler<Graph>& labeler, const string& file_name,
		string graph_name, Path<EdgeId> path1/* = Path<EdgeId> ()*/,
		Path<EdgeId> path2/* = Path<EdgeId> ()*/) {
	INFO("Writing graph '" << graph_name << "' to file " << file_name);
	omnigraph::WriteToFile(g, labeler, file_name, graph_name, path1, path2);
	INFO("Graph '" << graph_name << "' written to file " << file_name);
}

template<class Graph>
Path<typename Graph::EdgeId> FindGenomePath(const Sequence& genome,
		const Graph& g, const EdgeIndex<Graph>& index, size_t k) {
	SimpleSequenceMapper<Graph> srt(g, index, k + 1);
	return srt.MapSequence(genome);
}

template<class Graph>
MappingPath<typename Graph::EdgeId> FindGenomeMappingPath(
		const Sequence& genome, const Graph& g,
		const EdgeIndex<Graph>& index,
		const KmerMapper<Graph>& kmer_mapper, size_t k) {
	ExtendedSequenceMapper<Graph> srt(g, index, kmer_mapper, k + 1);
	return srt.MapSequence(genome);
}

template<class gp_t>
map<typename gp_t::graph_t::EdgeId, string> GraphColoring(const gp_t& gp, size_t k) {
	return PathColorer<typename gp_t::graph_t>(
			gp.g,
			FindGenomeMappingPath(gp.genome, gp.g, gp.index, gp.kmer_mapper, k).simple_path(),
			FindGenomeMappingPath(!gp.genome, gp.g, gp.index, gp.kmer_mapper, k).simple_path()).ColorPath();
}

void ProduceInfo(const Graph& g, const EdgeIndex<Graph>& index,
const omnigraph::GraphLabeler<Graph>& labeler, const Sequence& genome,
const string& file_name, const string& graph_name, size_t k) {
	CountStats(g, index, genome, k);
	Path<Graph::EdgeId> path1 = FindGenomePath(genome, g, index, k);
	Path<Graph::EdgeId> path2 = FindGenomePath(!genome, g, index, k);
	WriteToDotFile(g, labeler, file_name, graph_name, path1, path2);
}

void ProduceNonconjugateInfo(NCGraph& g, const EdgeIndex<NCGraph>& index
		, const Sequence& genome,
		const string& work_tmp_dir, const string& graph_name,
		const IdTrackHandler<NCGraph> &IdTrackLabelerResolved,
		size_t k) {

    WARN("Non-conjugate graph is pure shit, no stats for you, badass.");
    //CountStats(g, index, genome, k);
	//	omnigraph::WriteSimple( file_name, graph_name, g, IdTrackLabelerResolved);
	//	omnigraph::WriteSimple( work_tmp_dir, graph_name, g, IdTrackLabelerResolved);

}

void WriteGraphComponentsAlongGenome(const Graph& g,
		const IdTrackHandler<Graph>& int_ids,
		const EdgeIndex<Graph>& index,
		const KmerMapper<Graph>& kmer_mapper,
		const GraphLabeler<Graph>& labeler, const Sequence& genome,
		const string& folder, const string &file_name,
		size_t split_edge_length, size_t k) {

	INFO("Writing graph components along genome");

	typedef MappingPath<EdgeId> map_path_t;

	map_path_t path1 = FindGenomeMappingPath(genome, g, index, kmer_mapper, k);
	map_path_t path2 = FindGenomeMappingPath(!genome, g, index, kmer_mapper, k);

	make_dir(folder);
	WriteComponentsAlongGenome(g, labeler, folder + file_name,
			split_edge_length, path1, path2);

	INFO("Writing graph components along genome finished");
}

//todo refactoring needed: use graph pack instead!!!
void WriteGraphComponentsAlongContigs(const Graph& g,
		const EdgeIndex<Graph>& index,
		const KmerMapper<Graph>& kmer_mapper,
		const GraphLabeler<Graph>& labeler, 
        const Sequence& genome,
		const string& folder,
		size_t split_edge_length, size_t k) {

	INFO("Writing graph components along contigs");

	//typedef MappingPath<EdgeId> map_path_t;

	//typedef graph_pack<ConjugateDeBruijnGraph, K> gp_t;
	io::EasyReader contigs_to_thread(cfg::get().pos.contigs_to_analyze, false/*true*/);
	contigs_to_thread.reset();

	NewExtendedSequenceMapper<Graph> mapper(g, index, kmer_mapper, k + 1);

	MappingPath<EdgeId> path1 = FindGenomeMappingPath(genome, g, index, kmer_mapper, k);
	MappingPath<EdgeId> path2 = FindGenomeMappingPath(!genome, g, index, kmer_mapper, k);


    io::SingleRead read;
    while (!contigs_to_thread.eof()) {
        contigs_to_thread >> read;
        make_dir(folder + read.name());
        size_t component_vertex_number = 30;
        WriteComponentsAlongPath(g, labeler, folder + read.name() + "/" + "g.dot"
                , split_edge_length, component_vertex_number, mapper.MapSequence(read.sequence())
//                , Path<Graph::EdgeId>(), Path<Graph::EdgeId>(), true);
                , path1.simple_path(), path2.simple_path(), true);

        //todo delete
//    	ReliableSplitterAlongPath<Graph> splitter(g, component_vertex_number, split_edge_length, mapper.MapSequence(read.sequence()));
//    	vector<VertexId> comp_vert = splitter.NextComponent();
//    	GraphComponent<Graph> component(g, comp_vert.begin(), comp_vert.end());
//    	ConjugateDataPrinter<Graph> printer(component, g.int_ids());
//    	PrintBasicGraph<Graph>(folder + read.name() + "/" + "g", printer);
    	//todo end of delete

    }
	INFO("Writing graph components along contigs finished");
}

void WriteKmerComponent(conj_graph_pack &gp,
		const omnigraph::GraphLabeler<Graph>& labeler, const string& folder,
		const Path<Graph::EdgeId>& path1, const Path<Graph::EdgeId>& path2,
		runtime_k::RtSeq const& kp1mer) {
	VERIFY(gp.index.contains(kp1mer));
	EdgeNeighborhoodFinder<Graph> splitter(gp.g, gp.index.get(kp1mer).first, 50,
			*cfg::get().ds.IS);
	ComponentSizeFilter<Graph> filter(gp.g, *cfg::get().ds.IS, 2);
	PathColorer<Graph> colorer(gp.g, path1, path2);
	WriteComponents<Graph>(gp.g, splitter, filter, folder + "kmer.dot",
			*DefaultColorer(gp.g, path1, path2), labeler);
}

optional<runtime_k::RtSeq> FindCloseKP1mer(const conj_graph_pack &gp,
		size_t genome_pos, size_t k) {
	static const size_t magic_const = 200;
	for (size_t diff = 0; diff < magic_const; diff++) {
		for (int dir = -1; dir <= 1; dir += 2) {
			size_t pos = genome_pos + dir * diff;
			runtime_k::RtSeq kp1mer = gp.kmer_mapper.Substitute(
			        runtime_k::RtSeq (k + 1, gp.genome, pos));
			if (gp.index.contains(kp1mer))
				return optional<runtime_k::RtSeq>(kp1mer);
		}
	}
	return none;
}

void ProduceDetailedInfo(conj_graph_pack &gp,
		const omnigraph::GraphLabeler<Graph>& labeler, const string& folder,
		const string& file_name, const string& graph_name,
		info_printer_pos pos,
		size_t k) {
	auto it = cfg::get().info_printers.find(pos);
	VERIFY(it != cfg::get().info_printers.end());

	const debruijn_config::info_printer & config = it->second;

	if (config.print_stats) {
		INFO("Printing statistics for " << details::info_printer_pos_name(pos));
		CountStats(gp.g, gp.index, gp.genome, k);
	}

	typedef Path<Graph::EdgeId> path_t;
	path_t path1;
	path_t path2;

	if (config.detailed_dot_write || config.write_components
			|| !config.components_for_kmer.empty()
			|| config.write_components_along_genome
			|| config.write_components_along_contigs || config.save_full_graph
			|| !config.components_for_genome_pos.empty()) {
		path1 = FindGenomeMappingPath(gp.genome, gp.g, gp.index,
				gp.kmer_mapper, k).simple_path();
		path2 = FindGenomeMappingPath(!gp.genome, gp.g, gp.index,
				gp.kmer_mapper, k).simple_path();
//		path1 = FindGenomePath<K>(gp.genome, gp.g, gp.index);
//		path2 = FindGenomePath<K>(!gp.genome, gp.g, gp.index);
		make_dir(folder);
	}

	if (config.detailed_dot_write) {
		make_dir(folder + "error_loc/");
		DetailedWriteToDot(gp.g, labeler, folder + "error_loc/" + file_name,
				graph_name, path1, path2);
	}

	if (config.write_components) {
		make_dir(folder + "components/");
		size_t threshold = 500; //cfg::get().ds.IS ? *cfg::get().ds.IS : 250;
		WriteComponents(gp.g, threshold, folder + "components/" + file_name,
				*DefaultColorer(gp.g, path1, path2), labeler);
	}

	if (!config.components_for_kmer.empty()) {
		make_dir(folder + "kmer_loc/");
		WriteKmerComponent(gp, labeler, folder + "kmer_loc/", path1, path2,
		        runtime_k::RtSeq(k + 1, config.components_for_kmer.c_str()));
	}

	if (config.write_components_along_genome) {
		make_dir(folder + "along_genome/");
		size_t threshold = 500; //cfg::get().ds.IS ? *cfg::get().ds.IS : 250;
		WriteGraphComponentsAlongGenome(gp.g, gp.int_ids, gp.index,
				gp.kmer_mapper, labeler, gp.genome, folder, "along_genome/graph.dot",
				threshold, k);
	}

	if (config.write_components_along_contigs) {
		make_dir(folder + "along_contigs/");
		size_t threshold = 500; //cfg::get().ds.IS ? *cfg::get().ds.IS : 250;
		WriteGraphComponentsAlongContigs(gp.g, gp.index,
				gp.kmer_mapper, labeler, gp.genome, folder + "along_contigs/",
				threshold, k);
	}

	if (config.save_full_graph) {
		make_dir(folder + "full_graph_save/");
		ConjugateDataPrinter<Graph> printer(gp.g, gp.int_ids);
		PrintGraphPack(folder + "full_graph_save/graph", printer, gp);
	}

	if (!config.components_for_genome_pos.empty()) {
		string pos_loc_folder = folder + "pos_loc/";
		make_dir(pos_loc_folder);
		vector<string> positions;
		boost::split(positions, config.components_for_genome_pos,
				boost::is_any_of(" ,"), boost::token_compress_on);
		for (auto it = positions.begin(); it != positions.end(); ++it) {
			optional < runtime_k::RtSeq > close_kp1mer = FindCloseKP1mer(gp,
					boost::lexical_cast<int>(*it), k);
			if (close_kp1mer) {
				string locality_folder = pos_loc_folder + *it + "/";
				make_dir(locality_folder);
				WriteKmerComponent(gp, labeler, locality_folder, path1, path2,
						*close_kp1mer);
			} else {
				WARN(
						"Failed to find genome kp1mer close to the one at position "
								<< *it << " in the graph. Which is " << runtime_k::RtSeq (k + 1, gp.genome, boost::lexical_cast<int>(*it)));
			}
		}
	}
}

struct detail_info_printer {
	detail_info_printer(conj_graph_pack &gp,
			const omnigraph::GraphLabeler<Graph>& labeler, const string& folder,
			const string& file_name)

	:
			folder_(folder), func_(
					bind(&ProduceDetailedInfo, boost::ref(gp), boost::ref(labeler), _3,
							file_name, _2, _1, gp.k_value)),
							graph_(gp.g) {
	}

	void operator()(info_printer_pos pos,
			string const& folder_suffix = "") const {
		string pos_name = details::info_printer_pos_name(pos);
		VertexEdgeStat<conj_graph_pack::graph_t> stats(graph_);
		TRACE("Number of vertices : " << stats.vertices() << ", number of edges : " << stats.edges() << ", sum length of edges : " << stats.edge_length());
		func_(
				pos,
				pos_name,
                (path::append_path(folder_, (pos_name + folder_suffix)) + "/"));
	}

private:
	string folder_;
	boost::function<void(info_printer_pos, string const&, string const&)> func_;
	const conj_graph_pack::graph_t &graph_;
};

void WriteGraphComponents(const Graph& g, const EdgeIndex<Graph>& index,
const GraphLabeler<Graph>& labeler, const Sequence& genome,
const string& folder, const string &file_name,
size_t split_edge_length, size_t k) {
	make_dir(folder);

	VERIFY_MSG(false, "ololo");
//	WriteComponents(
//			g,
//			split_edge_length,
//			folder + file_name,
//			*DefaultColorer(FindGenomePath(genome, g, index, k),
//					FindGenomePath(!genome, g, index, k)), labeler);

}

string ConstructComponentName(string file_name, size_t cnt) {
	stringstream ss;
	ss << cnt;
	string res = file_name;
	res.insert(res.length(), ss.str());
	return res;
}

template<class graph_pack>
int PrintGraphComponents(const string& file_name, graph_pack& gp,
		size_t split_edge_length, PairedInfoIndex<Graph> &clustered_index) {
	LongEdgesInclusiveSplitter<Graph> inner_splitter(gp.g, split_edge_length);
	ComponentSizeFilter<Graph> checker(gp.g, split_edge_length, 2);
	FilteringSplitterWrapper<Graph> splitter(inner_splitter, checker);
	size_t cnt = 1;
	while (!splitter.Finished() && cnt <= 1000) {
		string component_name = ConstructComponentName(file_name, cnt).c_str();
		auto component = splitter.NextComponent();
		PrintWithClusteredIndex(component_name, gp, component.begin(),
				component.end(), clustered_index);
		cnt++;
	}
	return (cnt - 1);
}

template<class Graph>
vector<typename Graph::EdgeId> Unipath(const Graph& g, typename Graph::EdgeId e) {
	typedef typename Graph::EdgeId EdgeId;
	UniquePathFinder<Graph> unipath_finder(g);
	vector<EdgeId> answer = unipath_finder.UniquePathBackward(e);
	vector<EdgeId> forward = unipath_finder.UniquePathForward(e);
	for (size_t i = 1; i < forward.size(); ++i) {
		answer.push_back(forward[i]);
	}
	return answer;
}

template<class Graph>
double AvgCoverage(const Graph& g,
		const vector<typename Graph::EdgeId>& edges) {
	double total_cov = 0.;
	size_t total_length = 0;
	for (auto it = edges.begin(); it != edges.end(); ++it) {
		total_cov += g.coverage(*it) * g.length(*it);
		total_length += g.length(*it);
	}
	return total_cov / total_length;
}

template<class Graph>
bool PossibleECSimpleCheck(const Graph& g
		, typename Graph::EdgeId e) {
	return g.OutgoingEdgeCount(g.EdgeStart(e)) > 1 && g.IncomingEdgeCount(g.EdgeEnd(e)) > 1;
}

template<class Graph>
void ReportEdge(osequencestream_cov& oss
		, const Graph& g
		, typename Graph::EdgeId e
		, bool output_unipath = false
		, size_t solid_edge_length_bound = 0) {
	typedef typename Graph::EdgeId EdgeId;
	if (!output_unipath || (PossibleECSimpleCheck(g, e) && g.length(e) <= solid_edge_length_bound)) {
		TRACE("Outputting edge " << g.str(e) << " as single edge");
		oss << g.coverage(e);
		oss << g.EdgeNucls(e);
	} else {
		TRACE("Outputting edge " << g.str(e) << " as part of unipath");
		vector<EdgeId> unipath = Unipath(g, e);
		TRACE("Unipath is " << g.str(unipath));
		oss << AvgCoverage(g, unipath);
		TRACE("Merged sequence is of length " << MergeSequences(g, unipath).size());
		oss << MergeSequences(g, unipath);
	}
}

void OutputContigs(NonconjugateDeBruijnGraph& g,
		const string& contigs_output_filename,
		bool output_unipath = false,
		size_t solid_edge_length_bound = 0) {
	INFO("Outputting contigs to " << contigs_output_filename);
	osequencestream_cov oss(contigs_output_filename);
	for (auto it = g.SmartEdgeBegin(); !it.IsEnd(); ++it) {
		ReportEdge(oss, g, *it, output_unipath, solid_edge_length_bound);
	}
	DEBUG("Contigs written");
}

void OutputContigs(ConjugateDeBruijnGraph& g,
		const string& contigs_output_filename,
		bool output_unipath = false,
		size_t solid_edge_length_bound = 0) {
	INFO("Outputting contigs to " << contigs_output_filename);
	osequencestream_cov oss(contigs_output_filename);
	set<ConjugateDeBruijnGraph::EdgeId> edges;
	for (auto it = g.SmartEdgeBegin(); !it.IsEnd(); ++it) {
		if (edges.count(*it) == 0) {
			ReportEdge(oss, g, *it, output_unipath, solid_edge_length_bound);
			edges.insert(g.conjugate(*it));
		}
		//		oss << g.EdgeNucls(*it);
	}
	DEBUG("Contigs written");
}

void OutputSingleFileContigs(NonconjugateDeBruijnGraph& g,
		const string& contigs_output_dir) {
	INFO("Outputting contigs to " << contigs_output_dir);
	int n = 0;
	make_dir(contigs_output_dir);
	char n_str[20];
	for (auto it = g.SmartEdgeBegin(); !it.IsEnd(); ++it) {
		sprintf(n_str, "%d.fa", n);

		osequencestream oss(contigs_output_dir + n_str);

		//		osequencestream oss(contigs_output_dir + "tst.fasta");
		oss << g.EdgeNucls(*it);
		n++;
	}
	DEBUG("SingleFileContigs written");
}

void OutputSingleFileContigs(ConjugateDeBruijnGraph& g,
		const string& contigs_output_dir) {
	INFO("Outputting contigs to " << contigs_output_dir);
	int n = 0;
	make_dir(contigs_output_dir);
	char n_str[20];
	set<ConjugateDeBruijnGraph::EdgeId> edges;
	for (auto it = g.SmartEdgeBegin(); !it.IsEnd(); ++it) {
		if (edges.count(*it) == 0) {
			sprintf(n_str, "%d.fa", n);
			edges.insert(g.conjugate(*it));
			osequencestream oss(contigs_output_dir + n_str);
			oss << g.EdgeNucls(*it);
			n++;
		}
	}
	DEBUG("SingleFileContigs(Conjugate) written");
}

void tSeparatedStats(conj_graph_pack& gp, const Sequence& contig,
		PairedInfoIndex<conj_graph_pack::graph_t> &ind, size_t k) {
	typedef omnigraph::PairInfo<EdgeId> PairInfo;

	MappingPath<Graph::EdgeId> m_path1 = FindGenomeMappingPath(contig, gp.g,
			gp.index, gp.kmer_mapper, k);

	map<Graph::EdgeId, vector<pair<int, int>>> inGenomeWay;
	int CurI = 0;
	int gaps = 0;
	for (size_t i = 0; i < m_path1.size(); i++) {
		bool new_edge_added = false;
		EdgeId ei = m_path1[i].first;
		MappingRange mr = m_path1[i].second;
		int start = mr.initial_range.start_pos - mr.mapped_range.start_pos;
		if (inGenomeWay.find(ei) == inGenomeWay.end()) {
			vector<pair<int, int>> tmp;
			tmp.push_back(make_pair(CurI, start));
			inGenomeWay[ei] = tmp;
			CurI++;
			new_edge_added = true;
			DEBUG(
					"Edge " << gp.int_ids.str(ei) << " num " << CurI << " pos "
							<< start);
		} else {
			if (m_path1[i - 1].first == ei) {
				if (abs(
						start
								- inGenomeWay[ei][(inGenomeWay[ei].size() - 1)].second)
						> 50) {
					inGenomeWay[ei].push_back(make_pair(CurI, start));
					CurI++;
					new_edge_added = true;
					DEBUG(
							"Edge " << gp.int_ids.str(ei) << " num " << CurI
									<< " pos " << start);
				}
			} else {
				inGenomeWay[ei].push_back(make_pair(CurI, start));
				CurI++;
				new_edge_added = true;
				DEBUG(
						"Edge " << gp.int_ids.str(ei) << " num " << CurI
								<< " pos " << start);
			}
		}
		if (new_edge_added && (i > 0)) {
			if (gp.g.EdgeStart(ei) != gp.g.EdgeEnd(m_path1[i - 1].first)) {
				gaps++;
			}
		}
	}
	INFO(
			"Totaly " << CurI << " edges in genome path, with " << gaps
					<< "not adjacent conequences");
	vector<int> stats(10);
	vector<int> stats_d(10);
	int PosInfo = 0;
	int AllignedPI = 0;
	int ExactDPI = 0;
	int OurD = *cfg::get().ds.IS - *cfg::get().ds.RL;
	for (auto p_iter = ind.begin(), p_end_iter = ind.end();
			p_iter != p_end_iter; ++p_iter) {
		vector<PairInfo> pi = *p_iter;
		for (size_t j = 0; j < pi.size(); j++) {
			EdgeId left_edge = pi[j].first;
			EdgeId right_edge = pi[j].second;
			int dist = pi[j].d;
			if (dist < 0.001)
				continue;
			int best_d = 100;
			int best_t = 0;
			PosInfo++;
			DEBUG(
					"PairInfo " << gp.int_ids.str(left_edge) << " -- "
							<< gp.int_ids.str(right_edge) << " dist " << dist);
			bool ExactOnD = false;
			for (size_t left_i = 0; left_i < inGenomeWay[left_edge].size();
					left_i++)
				for (size_t right_i = 0;
						right_i < inGenomeWay[right_edge].size(); right_i++) {
					if (best_d
							> abs(
									inGenomeWay[right_edge][right_i].second
											- inGenomeWay[left_edge][left_i].second
											- dist)) {
						best_d = abs(
								inGenomeWay[right_edge][right_i].second
										- inGenomeWay[left_edge][left_i].second
										- dist);
						best_t = inGenomeWay[right_edge][right_i].first
								- inGenomeWay[left_edge][left_i].first;
						DEBUG("best d " << best_d);
						if ((inGenomeWay[right_edge][right_i].second
								- inGenomeWay[left_edge][left_i].second
								- (int) gp.g.length(left_edge) <= OurD)
								&& (inGenomeWay[right_edge][right_i].second
										- inGenomeWay[left_edge][left_i].second
										+ (int) gp.g.length(right_edge) >= OurD))
							ExactOnD = true;
						else
							ExactOnD = false;
					}
				}
			if (best_t > 5)
				best_t = 5;
			if (best_d < 100) {
				AllignedPI++;
				stats[best_t]++;
				if (ExactOnD) {
					stats_d[best_t]++;
					ExactDPI++;
				}
			}

		}
	}
	INFO(
			"Total positive pair info " << PosInfo << " alligned to genome "
					<< AllignedPI << " with exact distance " << ExactDPI);
	INFO(
			"t-separated stats Alligneg: 1 - " << stats[1] << " 2 - "
					<< stats[2] << " 3 - " << stats[3] << " 4 - " << stats[4]
					<< " >4 - " << stats[5]);
	INFO(
			"t-separated stats Exact: 1 - " << stats_d[1] << " 2 - "
					<< stats_d[2] << " 3 - " << stats_d[3] << " 4 - "
					<< stats_d[4] << " >4 - " << stats[5]);
}

template<class Graph, class Mapper>
class PosFiller {
	typedef typename Graph::EdgeId EdgeId;
	const Graph& g_;
	const Mapper& mapper_;
	EdgesPositionHandler<Graph>& edge_pos_;

public:
	PosFiller(const Graph& g, const Mapper& mapper,
			EdgesPositionHandler<Graph>& edge_pos) :
			g_(g), mapper_(mapper), edge_pos_(edge_pos) {

	}

	void Process(const Sequence& s, string name) const {
		//todo stupid conversion!

		return Process(io::SingleRead(name, s.str()));
	}

	void Process(const io::SingleRead& read) const {
//		Process(read.sequence(), read.name());
		MappingPath<EdgeId> path = mapper_.MapRead(read);
		const string& name = read.name();
		int cur_pos = 0;
		TRACE(
				"Contig " << name << " mapped on " << path.size()
						<< " fragments.");
		for (size_t i = 0; i < path.size(); i++) {
			EdgeId ei = path[i].first;
			MappingRange mr = path[i].second;
			int len = mr.mapped_range.end_pos - mr.mapped_range.start_pos;
			if (i > 0)
				if (path[i - 1].first != ei)
					if (g_.EdgeStart(ei) != g_.EdgeEnd(path[i - 1].first)) {
						TRACE(
								"Contig " << name
										<< " mapped on not adjacent edge. Position in contig is "
										<< path[i - 1].second.initial_range.start_pos
												+ 1
										<< "--"
										<< path[i - 1].second.initial_range.end_pos
										<< " and "
										<< mr.initial_range.start_pos + 1
										<< "--" << mr.initial_range.end_pos);
					}
			edge_pos_.AddEdgePosition(ei, mr.initial_range.start_pos + 1,
					mr.initial_range.end_pos, name, mr.mapped_range.start_pos + 1, mr.mapped_range.end_pos);
			cur_pos += len;
		}
	}

private:
	DECL_LOGGER("PosFiller");
};

template<class Graph, class Mapper>
void FillPos(const Graph& g, const Mapper& mapper,
		EdgesPositionHandler<Graph>& edge_pos,
		io::IReader<io::SingleRead>& stream) {
	PosFiller<Graph, Mapper> filler(g, mapper, edge_pos);
	io::SingleRead read;
	while (!stream.eof()) {
		stream >> read;
		filler.Process(read);
	}
}

template<class gp_t>
void FillPos(gp_t& gp,
		io::IReader<io::SingleRead>& stream) {
	typedef typename gp_t::graph_t Graph;
	typedef NewExtendedSequenceMapper<Graph> Mapper;
	Mapper mapper(gp.g, gp.index, gp.kmer_mapper, gp.k_value + 1);
	FillPos<Graph, Mapper>(gp.g, mapper, gp.edge_pos, stream);
}

template<class gp_t>
void FillPos(gp_t& gp, const Sequence& s, const string& name) {
	typedef typename gp_t::graph_t Graph;
	typedef NewExtendedSequenceMapper<Graph> Mapper;
	Mapper mapper(gp.g, gp.index, gp.kmer_mapper, gp.k_value + 1);
	PosFiller<Graph, Mapper>(gp.g, mapper, gp.edge_pos).Process(s, name);
}

//todo refactor!!!
class IdSettingReaderWrapper: public io::DelegatingReaderWrapper<io::SingleRead> {
	typedef io::DelegatingReaderWrapper<io::SingleRead> base;
	size_t next_id_;
public:
	IdSettingReaderWrapper(io::IReader<io::SingleRead>& reader, size_t start_id = 0) :
			base(reader), next_id_(start_id) {

	}

	/* virtual */
	IdSettingReaderWrapper& operator>>(io::SingleRead& read) {
		this->reader() >> read;
		read.ChangeName(ToString(next_id_++));
		return *this;
	}
};

class PrefixAddingReaderWrapper: public io::DelegatingReaderWrapper<io::SingleRead> {
	typedef io::DelegatingReaderWrapper<io::SingleRead> base;
	string prefix_;
public:
	PrefixAddingReaderWrapper(io::IReader<io::SingleRead>& reader,
			const string& prefix) :
			base(reader), prefix_(prefix) {

	}

	/* virtual */
	PrefixAddingReaderWrapper& operator>>(io::SingleRead& read) {
		this->reader() >> read;
		read.ChangeName(prefix_ + read.name());
		return *this;
	}
};

//deprecated, todo remove usages!!!
template<class gp_t>
void FillPos(gp_t& gp, const string& contig_file, string prefix) {
//	typedef typename gp_t::Graph::EdgeId EdgeId;
	INFO("Threading large contigs");
	io::Reader irs(contig_file);
	while(!irs.eof()) {
		io::SingleRead read;
		irs >> read;
		DEBUG("Contig " << read.name() << ", length: " << read.size());
		if (!read.IsValid()) {
			WARN("Attention: contig " << read.name() << " contains Ns");
			continue;
		}
		Sequence contig = read.sequence();
		if (contig.size() < 1500000) {
			//		continue;
		}
		FillPos(gp, contig, prefix + read.name());
	}
}

template<class gp_t>
void FillPosWithRC(gp_t& gp, const string& contig_file, string prefix) {
//  typedef typename gp_t::Graph::EdgeId EdgeId;
	INFO("Threading large contigs");
	io::EasyReader irs(contig_file, true);
	while(!irs.eof()) {
		io::SingleRead read;
		irs >> read;
		DEBUG("Contig " << read.name() << ", length: " << read.size());
		if (!read.IsValid()) {
			WARN("Attention: contig " << read.name() << " contains Ns");
			continue;
		}
		Sequence contig = read.sequence();
		if (contig.size() < 1500000) {
			//continue;
		}
		FillPos(gp, contig, prefix + read.name());
    }
}

////template<size_t k>
////deprecated, todo remove usages
//void FillPos(conj_graph_pack& gp, const Sequence& genome) {
//	FillPos(gp, genome, 0);
//}

void OutputWrongContigs(Graph& g, EdgeIndex<Graph>& index,
const Sequence& genome, size_t bound, const string &file_name, size_t k) {
    SimpleSequenceMapper<Graph> sequence_mapper(g, index, k + 1);
    Path<EdgeId> path1 = sequence_mapper.MapSequence(Sequence(genome));
    Path<EdgeId> path2 = sequence_mapper.MapSequence(!Sequence(genome));
    set<EdgeId> path_set;
    path_set.insert(path1.begin(), path1.end());
    path_set.insert(path2.begin(), path2.end());
    osequencestream os((cfg::get().output_dir + "/" + file_name).c_str());
    for (auto it = g.SmartEdgeBegin(); !it.IsEnd(); ++it) {
        if (path_set.count(*it) == 0 && g.length(*it) > 1000) {
            const Sequence &nucls = g.EdgeNucls(*it);
            os << nucls;
        }
    }
}

void OutputWrongContigs(conj_graph_pack& gp, size_t bound,
		const string &file_name) {
	OutputWrongContigs(gp.g, gp.index, gp.genome, bound, file_name, gp.k_value);
}



/*//		Graph& g, const EdgeIndex<k + 1, Graph>& index,
 //		const Sequence& genome, EdgesPositionHandler<Graph>& edgesPos, KmerMapper<k + 1, Graph>& kmer_mapper)
 {
 Path<typename Graph::EdgeId> path1 = FindGenomePath<K> (genome, gp.g, gp.index);
 int CurPos = 0;
 for (auto it = path1.sequence().begin(); it != path1.sequence().end(); ++it) {
 EdgeId ei = *it;
 gp.edge_pos.AddEdgePosition(ei, CurPos + 1, CurPos + g.length(ei));
 CurPos += g.length(ei);
 }

 CurPos = 0;
 Path<typename Graph::EdgeId> path2 = FindGenomePath<k> (!genome, g, index);
 for (auto it = path2.sequence().begin(); it != path2.sequence().end(); ++it) {
 CurPos -= g.length(*it);
 }

 for (auto it = path2.sequence().begin(); it != path2.sequence().end(); ++it) {
 EdgeId ei = *it;
 edgesPos.AddEdgePosition(ei, CurPos, CurPos + g.length(ei) - 1);
 CurPos += g.length(ei);
 }

 }
 */

template<class Graph>
size_t Nx(Graph &g, double percent){
	size_t sum_edge_length = 0;
	vector<size_t> lengths;
	for (auto iterator = g.SmartEdgeBegin(); !iterator.IsEnd(); ++iterator) {
		lengths.push_back(g.length(*iterator));
		sum_edge_length += g.length(*iterator);
	}
	sort(lengths.begin(), lengths.end());
	double len_perc = (1 - percent * 0.01) * (sum_edge_length);
	for(size_t i = 0; i < lengths.size(); i++){
		if (lengths[i] >= len_perc)
			return lengths[i];
		else
			len_perc -= lengths[i];
	}
	return 0;
}




}
