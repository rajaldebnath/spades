#pragma once

#include "redundant_contig_remover.hpp"

using namespace debruijn_graph;

namespace dipspades {

void OverlapgraphToDot(string dotfname, OverlapGraph & g, ContigStoragePtr stor){
	ofstream dot(dotfname.c_str());

//	cout << "Number of vertices - " << g.VerticesCount() << endl;
//	cout << "Number of contigs - " << stor->Size() << endl;
	VERIFY(g.VerticesCount() <= stor->Size());

	dot << "digraph Overlaped_paths {" << endl << "node[fontname=<Courier>]" << endl;

	auto vertices = g.Vertices();
	for(auto v = vertices.begin(); v != vertices.end(); v++){
		dot << *v << "[label=\"ID = #" << *v << ". " << *v << ", RC_ID = " <<
				stor->GetContigById(*v)->rc_id() << "\"]" << endl;
	}

	auto edges = g.Edges();
	for(auto e = edges.begin(); e != edges.end(); e++)
		dot << e->first << "->" << e->second << "[label=\"" << g.GetWeightOf(*e) << "\"]" << endl;

	dot << "}";
}

//--------------------------------------------------------------------------------------------

class OverlapCorrector : public LoopBulgeDeletionCorrector{
	size_t k_value_;

	struct overlap_res {
		bool correctness;
		bool size;

		overlap_res(bool over_corr, size_t over_size) :
			correctness(over_corr),
			size(over_size) { }

		overlap_res() :
			correctness(false),
			size(0) { }
	};

	// todo insert check of bulge sides
	overlap_res IsOverlapCorrect(vector<EdgeId> first_path, vector<size_t> first_pos,
			vector<EdgeId> last_path, vector<size_t> last_pos){

		VERIFY(first_pos.size() == last_pos.size());

		if(first_pos.size() <= 1)
			return overlap_res();

//		cout << "Left tail length - " << GetLeftTailLength(last_path, last_pos) << endl;
//		cout << "Right tail length - " << GetRightTailLength(first_path, first_pos) << endl;

		if(IsLeftTailCorrect(last_path, last_pos) && IsRightTailCorrect(first_path, first_pos)){

			size_t first_start = ConvInd(first_pos[0], first_path.size());
			size_t last_end = ConvInd(last_pos[last_pos.size() - 1], last_path.size());

			// check of reachment of left tail start
			bool is_left_tail_correct = true;
			if(IsLeftTailExist(last_path, last_pos) ){

				if(dsp_cfg::get().cc.tails_lie_on_bulges){
					VertexId start1 = g_.EdgeStart(first_path[0]);
					VertexId start2 = g_.EdgeStart(last_path[0]);

					auto path_searcher = DijkstraHelper<Graph>::CreateBackwardBoundedDijkstra(g_,
							dsp_cfg::get().pbr.max_bulge_nucls_len);
					path_searcher.run(start1);
					auto reached_vert1 = path_searcher.ReachedVertices();

					path_searcher.run(start2);
					auto reached_vert2 = path_searcher.ReachedVertices();

					for(size_t i = 0; i < first_start; i++){
						VertexId cur_vert = g_.EdgeStart(first_path[i]);
						reached_vert1.push_back(cur_vert);
					}

					bool common_vertex_exists = false;
					for(auto v1 = reached_vert1.begin(); v1 != reached_vert1.end(); v1++)
						for(auto v2 = reached_vert2.begin(); v2 != reached_vert2.end(); v2++)
							if(*v1 == *v2){
								common_vertex_exists = true;
								break;
							}
					is_left_tail_correct = common_vertex_exists;
				}
				else{
				}
			}

			if(!is_left_tail_correct)
				return overlap_res();

			// check of reachment of right tail start
			bool is_right_tail_correct = true;
			if(IsRightTailExist(first_path, first_pos)){

				if(dsp_cfg::get().cc.tails_lie_on_bulges){
					size_t first_path_size = first_path.size(),
							last_path_size = last_path.size();

					VertexId end1 = g_.EdgeStart(first_path[first_path_size - 1]);
					VertexId end2 = g_.EdgeStart(last_path[last_path_size - 1]);

					auto path_searcher = DijkstraHelper<Graph>::CreateBackwardBoundedDijkstra(g_,
							dsp_cfg::get().pbr.max_bulge_nucls_len);
					path_searcher.run(end1);
					auto reached_vert1 = path_searcher.ReachedVertices();

					path_searcher.run(end2);
					auto reached_vert2 = path_searcher.ReachedVertices();

					for(size_t i = last_end; i < last_path.size(); i++){
						VertexId cur_vert = g_.EdgeEnd(last_path[i]);
						reached_vert2.push_back(cur_vert);
					}

					bool common_vertex_exists = false;
					for(auto v1 = reached_vert1.begin(); v1 != reached_vert1.end(); v1++)
						for(auto v2 = reached_vert2.begin(); v2 != reached_vert2.end(); v2++)
							if(*v1 == *v2){
								common_vertex_exists = true;
								break;
							}
					is_right_tail_correct = common_vertex_exists;
				}
			}

			if(is_right_tail_correct)
				return overlap_res(true, GetLeftTailLength(last_path, last_pos) +
						GetRightTailLength(first_path, first_pos));
		}
		return overlap_res();

	}

	pair<overlap_res, overlap_res> ArePathsOverlapped(vector<EdgeId> path1, vector<size_t> pos1,
			vector<EdgeId> path2, vector<size_t> pos2){

		if(path1.size() == 0 || path2.size() == 0)
			return make_pair(overlap_res(), overlap_res());

		VERIFY(pos1.size() == pos2.size());

		if(pos1.size() <= 1)
			return make_pair(overlap_res(), overlap_res());

		if(!IsLCSCorrect(path1, pos1, path2, pos2))
			return make_pair(overlap_res(), overlap_res());

		return make_pair(IsOverlapCorrect(path2, pos2, path1, pos1), IsOverlapCorrect(path1, pos1, path2, pos2));
	}

	string get_composite_contig_name(size_t i, size_t length){
		stringstream ss;
		ss << i << "_contig_" << length << "_length";
		return ss.str();
	}

public:
	OverlapCorrector(Graph &g, size_t k_value, size_t min_overlap_length, VertexPathIndex &path_index) :
		LoopBulgeDeletionCorrector(g,
				k_value,
				dsp_cfg::get().cc.max_loop_length,
				dsp_cfg::get().pbr.max_bulge_nucls_len,
				min_overlap_length,
				path_index),
			k_value_(k_value) {}

	ContigStoragePtr Correct(ContigStoragePtr contigs) {
		OverlapGraph og;
		vector<size_t> vertices;
		vector<size_t> id, rc_id;
		for(size_t i = 0; i < contigs->Size(); i++){
			vertices.push_back((*contigs)[i]->id());
			id.push_back((*contigs)[i]->id());
			rc_id.push_back((*contigs)[i]->rc_id());
		}
		og.InitializeVertexSet(vertices, id, rc_id);

		vector<vector<VertexId> > seqs;
		for(size_t i = 0; i < contigs->Size(); i++){
			vector<VertexId> seq = GetListOfVertices((*contigs)[i]->path_seq());
			seqs.push_back(seq);
		}
		LCSCalculator<VertexId> lcs_calc;
		map<pair<size_t, size_t>, pair<Range, Range> > map_over;

		set<pair<int, int> > processed_pairs;

		for(size_t i = 0; i < contigs->Size(); i++){
			auto path1 = (*contigs)[i]->path_seq();
			size_t id1 = (*contigs)[i]->id();
			size_t rc_id1 = (*contigs)[i]->rc_id();
			auto contigs_for_processing = path_index_.GetPathsIntersectedWith(path1);
			for(auto it = contigs_for_processing.begin(); it != contigs_for_processing.end(); it++){
				size_t j = *it;
				size_t id2 = (*contigs)[j]->id();
				size_t rc_id2 = (*contigs)[j]->rc_id();
				bool need_process = !((i % 2 == 0 && i + 1 == j) || j <= i);
				need_process = need_process && (processed_pairs.find(pair<int, int>(rc_id1, rc_id2)) ==
						processed_pairs.end());
				if(need_process){
					processed_pairs.insert(pair<int, int>(id1, id2));
					auto path2 = (*contigs)[j]->path_seq();
					auto lcs_res = lcs_calc.LCS(seqs[i], seqs[j]);
					vector<size_t> pos1, pos2;
					auto pos_vectors_pair = GetBestPosVectors(lcs_calc, path1, seqs[i], path2, seqs[j], lcs_res);
					pos1 = pos_vectors_pair.first;
					pos2 = pos_vectors_pair.second;

					{
						TRACE("--------------------------------");
						size_t id_i = id1, id_j = id2;
						TRACE("Indexes " << i << " " << j );
						TRACE("IDs " << id_i << " " << id_j);
						TRACE("LCS string : " << VerticesVectorToString(g_, lcs_res));
						TRACE("Path1. " << SimplePathWithVerticesToString(g_, path1));
						TRACE("Pos1. "  << VectorToString<size_t>(pos1));
						TRACE("Path2. " << SimplePathWithVerticesToString(g_, path2));
						TRACE("Pos2. "  << VectorToString<size_t>(pos2));
					}

					// Overlapping
					auto overlap_result = ArePathsOverlapped(path1, pos1, path2, pos2);
					bool is_overlaped = overlap_result.first.correctness ||
							overlap_result.second.correctness;

//					cout << "first -> second - " << overlap_result.second.correctness  <<
//							" with " << overlap_result.second.size << endl;
//					cout << "second -> first - " << overlap_result.first.correctness <<
//							" with " << overlap_result.first.size << endl;

					if(is_overlaped){

						size_t first_id, last_id;
						vector<EdgeId> first_path, last_path;
						vector<size_t> first_pos, last_pos;

						if(overlap_result.first.correctness && overlap_result.second.correctness){
							if(overlap_result.first.size < overlap_result.second.size){
								first_id = id2; last_id = id1;
							}
							else {
								first_id = id1; last_id = id2;
							}
						}
						else{
							if(overlap_result.first.correctness) {
								first_id = id2; last_id = id1;
							}
							else {
								first_id = id1; last_id = id2;
							}
						}

						first_path = (first_id == id1) ? path1 : path2;
						last_path = (last_id == id1) ? path1 : path2;
						first_pos = (first_id == id1) ? pos1 : pos2;
						last_pos = (last_id == id1) ? pos1 : pos2;

						size_t rc_first_id = contigs->GetContigById(first_id)->rc_id();
						size_t rc_last_id = contigs->GetContigById(last_id)->rc_id();

						size_t lcs_len1 = GetLCSLengthByPath(path1, pos1);
						size_t lcs_len2 = GetLCSLengthByPath(path2, pos2);

						size_t over_weight = max<size_t>(lcs_len1, lcs_len2);

						if(over_weight >= dsp_cfg::get().cc.min_overlap_size){
							og.AddNeighVertices(first_id, last_id, over_weight);
							og.AddNeighVertices(rc_last_id, rc_first_id, over_weight);
						}

						Range overlap_first(first_pos[0], first_pos[first_pos.size() - 1]);
						Range overlap_last(last_pos[0], last_pos[last_pos.size() - 1]);

						map_over[pair<size_t, size_t>(first_id, last_id)] = pair<Range, Range>(overlap_first,
								overlap_last);

						Range overlap_first_rc(first_path.size() - overlap_first.end_pos,
								first_path.size() - overlap_first.start_pos);
						Range overlap_last_rc(last_path.size() - overlap_last.end_pos,
								last_path.size() - overlap_last.start_pos);
						map_over[pair<size_t , size_t >(rc_last_id, rc_first_id)] = pair<Range, Range>(overlap_last_rc,
								overlap_first_rc);

						TRACE(first_id << " - " << last_id << ". " << overlap_first.start_pos << " - " <<
								overlap_first.end_pos << ", " << overlap_last.start_pos << " - " <<
								overlap_last.end_pos);

						TRACE(rc_last_id << " - " << rc_first_id << ". " << overlap_last_rc.start_pos << " - " <<
								overlap_last_rc.end_pos << ", " << overlap_first_rc.start_pos << " - " <<
								overlap_first_rc.end_pos);
					}
				}
			}
		}

//		cout << "Overlap map" << endl;
//		for(auto it = map_over.begin(); it != map_over.end(); it++){
//			Range r_first = it->second.first;
//			Range r_second = it->second.second;
//
//			cout << it->first.first << " - " << it->first.second << ". " << r_first.start_pos << " - " <<
//					r_first.end_pos << ", " << r_second.start_pos << " - " << r_second.end_pos << endl;
//		}

		string fname = dsp_cfg::get().io.output_dir + "default_overlap_graph.dot";
		OverlapgraphToDot(fname, og, contigs);

		INFO("Overlap graph with " + ToString(og.Vertices().size()) + " vertices and " +
				ToString(og.Edges().size()) + " edges constructed");

		auto og_vertices = og.Vertices();
		auto edges = og.Edges();

		SimplifyOverlapGraph(og, 10, 5);

		INFO("Simplified overlap graph contains " + ToString(og.Vertices().size()) + " vertices and " +
				ToString(og.Edges().size()) + " edges");

		fname = dsp_cfg::get().io.output_dir + "simplified_overlap_graph.dot";
		OverlapgraphToDot(fname, og, contigs);

		UniquePathsSearcher ps(og);
		auto paths = ps.FindLongPaths();
		TRACE(paths.size() << " were searched");

		ContigStoragePtr new_storage(new SimpleContigStorage());
		size_t i = 1;
		for(auto p = paths.begin(); p != paths.end(); p++){
			VERIFY(p->size() > 0);
			if(p->size() == 1){
				TRACE("Contig" << i << " is simple");
				auto contig = contigs->GetContigById((*p)[0]);
				MappingContigPtr new_rc(new ReplacedNameMappingContig(contig,
						get_composite_contig_name(i, contig->length())));
				new_storage->Add(new_rc);
			}
			else{
				TRACE("Contig" << i << " is composite");

				vector<pair<Range, Range> > overlaps;
				vector<MappingContigPtr> mc_vect;
				for(size_t i = 0; i < p->size() - 1; i++){
					overlaps.push_back(map_over[pair<size_t , size_t >((*p)[i], (*p)[i + 1])]);
				}
				for(auto id = p->begin(); id != p->end(); id++){
 					mc_vect.push_back(contigs->GetContigById(*id));
				}

				MappingContigPtr new_mc(new CompositeMappingContig(g_, k_value_,
						mc_vect, overlaps));
				new_mc->ChangeName(get_composite_contig_name(i, new_mc->length()));
				new_storage->Add(new_mc);
			}
			i++;
		}

		return new_storage;
	}
};

}
