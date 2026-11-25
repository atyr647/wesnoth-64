/*
 * boost::iostreams::filtering_stream for the N64 build: the two-element
 * chains Wesnoth uses (a gzip compressor or decompressor, then the stream),
 * implemented with zlib. This is what Wesnoth's compressed WML goes through:
 * the config cache and saved games (.gz). Boost itself is not part of the
 * console build.
 *
 * bzip2 is not supported (nothing on the console writes .bz2 files); those
 * chains pass the data through unchanged.
 */
#pragma once

#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <streambuf>
#include <vector>

#include <zlib.h>

namespace boost {
namespace iostreams {

struct output {};
struct input {};

namespace n64_detail {

inline voidpf z_alloc(voidpf, uInt n, uInt size) { return ::operator new(std::size_t(n) * size, std::nothrow); }
inline void z_free(voidpf, voidpf p) { ::operator delete(p); }

/** Deflates (gzip format) into the downstream stream. */
class gzip_out_buf : public std::streambuf
{
public:
	gzip_out_buf(std::ostream& sink, int level) : sink_(sink), in_(32768), out_(32768)
	{
		std::memset(&zs_, 0, sizeof zs_);
		zs_.zalloc = z_alloc;
		zs_.zfree = z_free;
		ok_ = deflateInit2(&zs_, level < 0 || level > 9 ? Z_DEFAULT_COMPRESSION : level,
			Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) == Z_OK;
		setp(in_.data(), in_.data() + in_.size());
	}
	~gzip_out_buf() override { finish(); }

	void finish()
	{
		if(!ok_ || done_) return;
		deflate_pending(Z_FINISH);
		deflateEnd(&zs_);
		done_ = true;
		sink_.flush();
	}

protected:
	int_type overflow(int_type c) override
	{
		if(!deflate_pending(Z_NO_FLUSH)) return traits_type::eof();
		if(!traits_type::eq_int_type(c, traits_type::eof())) {
			*pptr() = traits_type::to_char_type(c);
			pbump(1);
		}
		return traits_type::not_eof(c);
	}
	int sync() override { return deflate_pending(Z_SYNC_FLUSH) ? 0 : -1; }

private:
	bool deflate_pending(int flush)
	{
		if(!ok_ || done_) return false;
		zs_.next_in = reinterpret_cast<Bytef*>(pbase());
		zs_.avail_in = static_cast<uInt>(pptr() - pbase());
		int r;
		do {
			zs_.next_out = reinterpret_cast<Bytef*>(out_.data());
			zs_.avail_out = static_cast<uInt>(out_.size());
			r = deflate(&zs_, flush);
			if(r == Z_STREAM_ERROR) return false;
			const std::size_t have = out_.size() - zs_.avail_out;
			if(have) sink_.write(out_.data(), static_cast<std::streamsize>(have));
		} while(zs_.avail_out == 0 || (flush == Z_FINISH && r != Z_STREAM_END));
		setp(in_.data(), in_.data() + in_.size());
		return bool(sink_);
	}

	std::ostream& sink_;
	z_stream zs_;
	std::vector<char> in_, out_;
	bool ok_ = false, done_ = false;
};

/** Inflates a gzip (or zlib) stream read from the upstream stream. */
class gzip_in_buf : public std::streambuf
{
public:
	explicit gzip_in_buf(std::istream& src) : src_(src), in_(32768), out_(32768)
	{
		std::memset(&zs_, 0, sizeof zs_);
		zs_.zalloc = z_alloc;
		zs_.zfree = z_free;
		ok_ = inflateInit2(&zs_, 15 + 32) == Z_OK;   // gzip or zlib header
		setg(out_.data(), out_.data(), out_.data());
	}
	~gzip_in_buf() override { if(ok_) inflateEnd(&zs_); }

protected:
	int_type underflow() override
	{
		if(gptr() < egptr()) return traits_type::to_int_type(*gptr());
		if(!ok_ || ended_) return traits_type::eof();
		zs_.next_out = reinterpret_cast<Bytef*>(out_.data());
		zs_.avail_out = static_cast<uInt>(out_.size());
		while(zs_.avail_out == out_.size()) {
			if(zs_.avail_in == 0) {
				src_.read(in_.data(), static_cast<std::streamsize>(in_.size()));
				zs_.next_in = reinterpret_cast<Bytef*>(in_.data());
				zs_.avail_in = static_cast<uInt>(src_.gcount());
				if(zs_.avail_in == 0) break;              // truncated input: end of data
			}
			const int r = inflate(&zs_, Z_NO_FLUSH);
			if(r == Z_STREAM_END) {
				// Several gzip members may follow one another.
				if(zs_.avail_in == 0 && src_.peek() == std::char_traits<char>::eof()) {
					ended_ = true;
					break;
				}
				inflateReset(&zs_);
			} else if(r != Z_OK && r != Z_BUF_ERROR) {
				throw gzip_error(r);
			}
		}
		const std::size_t have = out_.size() - zs_.avail_out;
		setg(out_.data(), out_.data(), out_.data() + have);
		return have ? traits_type::to_int_type(*gptr()) : traits_type::eof();
	}

private:
	std::istream& src_;
	z_stream zs_;
	std::vector<char> in_, out_;
	bool ok_ = false, ended_ = false;
};

/** Passes everything through to another stream buffer (unsupported filters). */
inline std::streambuf* passthrough(std::ios& s) { return s.rdbuf(); }

} // namespace n64_detail

template<typename Mode>
class filtering_stream : public std::ostream
{
public:
	filtering_stream() : std::ostream(nullptr) {}
	~filtering_stream() override
	{
		if(gz_) gz_->finish();
	}

	void push(const gzip_compressor& c) { level_ = c.level; gzip_ = true; }
	void push(const bzip2_compressor&) { gzip_ = false; }
	void push(std::ostream& sink)
	{
		if(gzip_) {
			gz_ = std::make_unique<n64_detail::gzip_out_buf>(sink, level_);
			rdbuf(gz_.get());
		} else {
			rdbuf(n64_detail::passthrough(sink));
		}
		clear();
	}

	void pop() {}
	bool empty() const { return rdbuf() == nullptr; }
	void reset() { if(gz_) gz_->finish(); rdbuf(nullptr); gz_.reset(); }

private:
	std::unique_ptr<n64_detail::gzip_out_buf> gz_;
	int level_ = Z_DEFAULT_COMPRESSION;
	bool gzip_ = false;
};

template<>
class filtering_stream<input> : public std::istream
{
public:
	filtering_stream() : std::istream(nullptr) {}

	void push(const gzip_decompressor&) { gzip_ = true; }
	void push(const bzip2_decompressor&) { gzip_ = false; }
	void push(std::istream& src)
	{
		if(gzip_) {
			gz_ = std::make_unique<n64_detail::gzip_in_buf>(src);
			rdbuf(gz_.get());
		} else {
			rdbuf(n64_detail::passthrough(src));
		}
		clear();
	}

	void pop() {}
	bool empty() const { return rdbuf() == nullptr; }
	void reset() { rdbuf(nullptr); gz_.reset(); }

private:
	std::unique_ptr<n64_detail::gzip_in_buf> gz_;
	bool gzip_ = false;
};

} // namespace iostreams
} // namespace boost
