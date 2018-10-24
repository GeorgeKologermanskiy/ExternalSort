#include <string>
#include <fstream>
#include <iostream>
#include <cmath>
#include <vector>
#include <iterator>
#include <cstdio>
#include <algorithm>
#include <string>

// Interface

template<typename T>
void serialize(T value, std::ostream& out) {
	out.write(reinterpret_cast<char*>(&value), sizeof(T));
}

template<typename T>
T deserialize(std::istream& in) {
	T value;
	in.read(reinterpret_cast<char*>(&value), sizeof(T));
	return value;
}

template<typename T>
class SerializeIterator {
public:
	typedef void value_type;
	typedef void difference_type;
	typedef void pointer;
	typedef void reference;
	typedef std::output_iterator_tag iterator_category;

	explicit SerializeIterator(std::ostream& stream) {
		stream_ = &stream;
	}

	SerializeIterator& operator=(const T& value) {
		serialize(value, *stream_);
		return *this;
	}
	SerializeIterator& operator*() { return *this; } // does nothing
	SerializeIterator& operator++() { return *this; } // does nothing
	SerializeIterator& operator++(int) { return *this; } // does nothing

private:
	std::ostream* stream_;
};

template<typename T>
class DeserializeIterator {
public:
	typedef T value_type;
	typedef std::ptrdiff_t difference_type;
	typedef T* pointer;
	typedef T& reference;
	typedef std::input_iterator_tag iterator_category;

	DeserializeIterator() {}

	explicit DeserializeIterator(std::istream& stream) {
		stream_ = &stream;
		value_ = 0;
		isEnd_ = false;
		++*this;
	}

	const T& operator*() const {
		return value_;
	}

	const T& operator->() const {
		return operator*();
	}

	DeserializeIterator& operator++() {
		value_ = deserialize<value_type>(*stream_);
		if (stream_->eof())
			isEnd_ = true;
		return *this;
	}

	DeserializeIterator operator++(int) {
		DeserializeIterator it = *this;
		++*this;
		return it;
	}

	bool isEnd() const {
		return isEnd_;
	}

private:
	std::istream* stream_;
	T value_;
	bool isEnd_;
};

template<typename T>
bool operator==(const DeserializeIterator<T>& first, const DeserializeIterator<T>& second) {
	return &first == &second;
}

template<typename T>
bool operator!=(const DeserializeIterator<T>& first, const DeserializeIterator<T>& second) {
	return !(first == second);
}

std::string tempFilename() {
	static int NumberOfFile = 0;
	++NumberOfFile;
	std::string s = "";
	for (int x = NumberOfFile; x > 0; x /= 10)
		s += (x % 10 + '0');
	reverse(s.begin(), s.end());
	s += ".txt";
	return s;
}

template<typename InputIter, typename OutputIter, typename Merger>
class ExternalAlgoritm {
public:
	typedef typename std::iterator_traits<InputIter>::value_type value_type;

	ExternalAlgoritm(InputIter begin, InputIter end,
		size_t size, size_t maxObjectsInMemory,
		OutputIter out) :begin_(begin), end_(end), size_(size),
		maxObjectsInMemory_(maxObjectsInMemory), out_(out) {
		countOfFiles_ = (size_ + maxObjectsInMemory_ - (size_t)(1)) / maxObjectsInMemory_;
		fstreams_ = new std::fstream[countOfFiles_];
		filenames_ = std::vector<std::string>(countOfFiles_, "");
	}

	~ExternalAlgoritm() {
		for (size_t i = 0; i < countOfFiles_; i++) {
			remove(filenames_[i].c_str());
		}
		delete[] fstreams_;
	}

	void run() {
		for (size_t i = 0; i < countOfFiles_; i++) {
			filenames_[i] = tempFilename();
			fstreams_[i].open(filenames_[i], std::fstream::out | std::fstream::binary);
			SerializeIterator<value_type> Write(fstreams_[i]);
			std::vector<value_type> now;
			for (size_t j = 0; j < maxObjectsInMemory_ && i * maxObjectsInMemory_ + j < size_; j++) {
				now.push_back(*begin_);
				++begin_;
			}
			prepare(now);
			for (size_t i = 0; i < now.size(); ++i)
				Write = now[i];
			fstreams_[i].close();
		}
		std::vector<DeserializeIterator<value_type>> streams(countOfFiles_);
		for (size_t i = 0; i < countOfFiles_; ++i) {
			fstreams_[i].open(filenames_[i], std::fstream::in | std::fstream::binary);
			streams[i] = DeserializeIterator<value_type>(fstreams_[i]);
		}
		Merger M(streams);
		while (M.hasNext()) {
			*out_ = M.next();
			++out_;
		}
	}

private:
	virtual void prepare(std::vector<value_type>& data) = 0;

	InputIter begin_;
	InputIter end_;
	size_t size_;
	size_t maxObjectsInMemory_;
	OutputIter out_;

	size_t countOfFiles_;
	std::fstream* fstreams_;
	std::vector<std::string> filenames_;
};

template<class T>
struct DeserializerCompare {
	bool operator()(const DeserializeIterator<T>& first, const DeserializeIterator<T>& second) {
		return (*first > *second);
	}
};

template<class T>
class SortMerger {
public:
	explicit SortMerger(const std::vector<DeserializeIterator<T> >& deserializers) {
		deserializers_ = deserializers;
		std::make_heap(deserializers_.begin(), deserializers_.end(), DeserializerCompare<T>());
	}

	bool hasNext() const {
		return (int)(deserializers_.size()) > 0;
	}
	T next() {
		if (!hasNext())return 0;
		T ret = deserializers_[0].operator->();
		std::pop_heap(deserializers_.begin(), deserializers_.end(), DeserializerCompare<T>());
		++deserializers_.back();
		if (!deserializers_.back().isEnd()) {
			std::push_heap(deserializers_.begin(), deserializers_.end(), DeserializerCompare<T>());
		}
		else {
			deserializers_.pop_back();
		}
		return ret;
	}

private:
	std::vector<DeserializeIterator<T> > deserializers_;
};

template<typename InputIter, typename OutputIter>
class ExternalSort : public ExternalAlgoritm<
	InputIter, OutputIter, SortMerger<typename std::iterator_traits<InputIter>::value_type> > {
public:
	typedef ExternalAlgoritm<
		InputIter, OutputIter, SortMerger<typename std::iterator_traits<InputIter>::value_type> > Base;

	ExternalSort(InputIter begin, InputIter end,
		size_t size, size_t maxObjectsInMemory,
		OutputIter out) :Base(begin, end, size, maxObjectsInMemory, out) {}

private:
	virtual void prepare(std::vector<typename Base::value_type>& container) {
		std::sort(container.begin(), container.end());
	}
};

template<class T>
class ReverseMerger {
public:
	explicit ReverseMerger(const std::vector<DeserializeIterator<T> >& deserializers) {
		deserializers_ = deserializers;
	}

	bool hasNext() const {
		return ((int)(deserializers_.size()) > 0);
	}
	T next() {
		T ret = deserializers_.back().operator->();
		++deserializers_.back();
		if (deserializers_.back().isEnd())deserializers_.pop_back();
		return ret;
	}

private:
	std::vector<DeserializeIterator<T> > deserializers_;
};

template<typename InputIter, typename OutputIter>
class ExternalReverse : public ExternalAlgoritm<
	InputIter, OutputIter, ReverseMerger<typename std::iterator_traits<InputIter>::value_type> > {
public:
	typedef ExternalAlgoritm<
		InputIter, OutputIter, ReverseMerger<typename std::iterator_traits<InputIter>::value_type> > Base;

	ExternalReverse(InputIter begin, InputIter end,
		size_t size, size_t maxObjectsInMemory,
		OutputIter out) :Base(begin, end, size, maxObjectsInMemory, out) {}
private:
	virtual void prepare(std::vector<typename Base::value_type>& container) {
		std::reverse(container.begin(), container.end());
	}
};

// Implementation

int main()
{
	std::ifstream ifs("input.txt");
	std::ofstream ofs("output.txt");
	size_t type, count, max;
	ifs >> type >> count >> max;

	if (type == 1) {
		ExternalSort<
			std::istream_iterator<int>,
			std::ostream_iterator<int>
		> alg(
			std::istream_iterator<int>(ifs), std::istream_iterator<int>(),
			count, max,
			std::ostream_iterator<int>(ofs, " "));
		alg.run();
	}
	else {
		ExternalReverse<
			std::istream_iterator<int>,
			std::ostream_iterator<int>
		> alg(
			std::istream_iterator<int>(ifs), std::istream_iterator<int>(),
			count, max,
			std::ostream_iterator<int>(ofs, " "));
		alg.run();
	}
	return 0;
}