#pragma once

#include "io/reader.hpp"
#include "io/delegating_reader_wrapper.hpp"
#include "io/rc_reader_wrapper.hpp"
#include "io/filtering_reader_wrapper.hpp"
#include "io/careful_filtering_reader_wrapper.hpp"
#include "io/single_read.hpp"

namespace io {
//todo refactor, and maybe merge them once again
class EasyReader : public DelegatingReaderWrapper<io::SingleRead> {
	explicit EasyReader(const EasyReader& reader);
	void operator=(const EasyReader& reader);

	Reader raw_reader_;
//	FilteringReaderWrapper<ReadType> filtered_reader_;
	CarefulFilteringReaderWrapper<io::SingleRead> filtered_reader_;
	RCReaderWrapper<io::SingleRead> rc_reader_;

public:
	explicit EasyReader(const io::SingleRead::FilenameType& filename,
			bool rc,
			OffsetType offset_type = PhredOffset)
	: raw_reader_(filename, offset_type),
	  filtered_reader_(raw_reader_),
	  rc_reader_(filtered_reader_) {
		if (rc) {
			Init(rc_reader_);
		} else {
			Init(filtered_reader_);
		}
	}


  /*
   * Default destructor.
   */
  /* virtual */ ~EasyReader() {
  }

};

class PairedEasyReader
	: public DelegatingReaderWrapper<io::PairedRead>
{
	scoped_ptr<IReader<io::PairedRead>> raw_reader_;
	CarefulFilteringReaderWrapper<io::PairedRead> filtered_reader_;
	RCReaderWrapper<io::PairedRead> rc_reader_;

public:
  PairedEasyReader(const io::PairedRead::FilenamesType& filenames,
		  	  	  bool rc,
                  size_t insert_size,
                  bool change_read_order = false,
                  OffsetType offset_type = PhredOffset)
      : raw_reader_(new SeparateReader(filenames, insert_size, change_read_order, offset_type))
  	  , filtered_reader_(*raw_reader_)
  	  , rc_reader_(filtered_reader_)
  {
		if (rc) {
			Init(rc_reader_);
		} else {
			Init(filtered_reader_);
		}
  }

  PairedEasyReader(const std::string& filename,
		  	  	  bool rc,
                  size_t insert_size,
                  bool change_read_order = false,
                  OffsetType offset_type = PhredOffset)
      : raw_reader_(new MixedReader(filename, insert_size, change_read_order, offset_type))
  	  , filtered_reader_(*raw_reader_)
  	  , rc_reader_(filtered_reader_)
  {
		if (rc) {
			Init(rc_reader_);
		} else {
			Init(filtered_reader_);
		}
  }
};

}
