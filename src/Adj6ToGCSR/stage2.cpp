#include "stage.h"
#include "type.h"
#include "util.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/parallel_scan.h>
#include <tbb/parallel_sort.h>
#include <thread>

static auto dedup(sp<std::vector<E32>> in)
{
	// init
	auto out = makeSp<std::vector<E32>>(in->size());

	std::vector<std::atomic<uint32_t>> bitvec(size_t(ceil(double(in->size()) / 32.0)));

	auto getBit = [&bitvec](size_t const i) {
		return (bitvec[i / 32].load() & (1 << (i % 32))) != 0;
	};
	auto setBit = [&bitvec](size_t const i) { bitvec[i / 32].fetch_or(1 << (i % 32)); };

	std::vector<uint64_t> pSumRes(in->size() + 1);

	// printf("input size: %ld, bitvec size: %ld, pSumRes size: %ld\n", in->size(), bitvec.size(),
	// pSumRes.size());
	// prepare bit array
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, bitvec.size()),
		[&](tbb::blocked_range<size_t> const & r) {
			for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
				for (size_t offset = 0; offset < r.grainsize(); offset++) {
					auto i = grain + offset;

					bitvec[i] = 0;
				}
			}
		},
		tbb::auto_partitioner());

	// printf("insz: %ld, bitarray prepared\n", in->size());

	// prepare exclusive sum array
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, pSumRes.size()),
		[&](tbb::blocked_range<size_t> const & r) {
			for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
				for (size_t offset = 0; offset < r.grainsize(); offset++) {
					auto i = grain + offset;

					pSumRes[i] = 0;
				}
			}
		},
		tbb::auto_partitioner());

	// printf("insz: %ld, pSumRes prepared\n", in->size());

	// sort
	tbb::parallel_sort(in->begin(), in->end(), [&](E32 const & l, E32 const & r) {
		return (l[0] < r[0]) || ((l[0] == r[0]) && (l[1] < r[1]));
	});

	// printf("insz: %ld, sorted\n", in->size());
	// set bit 1 which is left != right (not the case: left == right)
	std::atomic<uint64_t> ones;
	ones.store(0);
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, in->size() - 1),
		[&](tbb::blocked_range<size_t> const & r) {
			uint64_t myOnes = 0;
			for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
				for (size_t offset = 0; offset < r.grainsize(); offset++) {
					auto curr = grain + offset;
					auto next = grain + offset + 1;
					if ((in->at(curr)[0] != in->at(next)[0]) ||
						(in->at(curr)[1] != in->at(next)[1])) {
						setBit(curr);
						myOnes++;
					}
				}
			}
			ones.fetch_add(myOnes);
		},
		tbb::auto_partitioner());
	setBit(in->size() - 1);
	ones.fetch_add(1);

	// printf("insz:%ld, bit setted\n", in->size());

	// exclusive sum
	tbb::parallel_scan(
		tbb::blocked_range<size_t>(0, in->size()),
		0L, // very important! not be just '0'
		[&](tbb::blocked_range<size_t> const & r, uint64_t sum, bool isFinalScan) {
			auto temp = sum;
			for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
				for (size_t offset = 0; offset < r.grainsize(); offset++) {
					auto i = grain + offset;
					temp += (getBit(i) ? 1 : 0);
					if (isFinalScan) {
						pSumRes[i + 1] = temp;
					}
				}
			}
			return temp;
		},
		[&](size_t const & l, size_t const & r) { return l + r; },
		tbb::auto_partitioner());

	// printf("insz:%ld, ones: %ld\n", in->size(), ones.load());

	out->resize(ones);

	// printf("insz:%ld, resized\n", in->size());

	// reduce out vector
	tbb::parallel_for(
		tbb::blocked_range<size_t>(0, in->size()),
		[&](tbb::blocked_range<size_t> const & r) {
			for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
				for (size_t offset = 0; offset < r.grainsize(); offset++) {
					auto i = grain + offset;

					if (getBit(i)) {
						// try {
						out->at(pSumRes[i]) = in->at(i);
						//} catch (std::out_of_range & e) {
						//	fprintf(stderr, "%s %ld\n", e.what(), i);
						// exit(EXIT_FAILURE);
						//}
					}
				}
			}
		},
		tbb::auto_partitioner());

	// printf("insz:%ld, outvector writed\n", in->size());

	return out;
}

static void writeCSR(fs::path const outTarget, sp<std::vector<E32>> in)
{

	auto fillRowAndPtr = std::thread([&, in] {
		// fill
		std::vector<std::atomic<uint32_t>> bitvec(size_t(ceil(double(in->size()) / 32.0)));

		auto getBit = [&bitvec](size_t const i) {
			return (bitvec[i / 32].load() & (1 << (i % 32))) != 0;
		};
		auto setBit = [&bitvec](size_t const i) { bitvec[i / 32].fetch_or(1 << (i % 32)); };

		std::vector<uint64_t> pSumRes(in->size() + 1);

		// prepare bit array
		tbb::parallel_for(
			tbb::blocked_range<size_t>(0, bitvec.size()),
			[&](tbb::blocked_range<size_t> const & r) {
				for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
					for (size_t offset = 0; offset < r.grainsize(); offset++) {
						auto i = grain + offset;

						bitvec[i] = 0;
					}
				}
			},
			tbb::auto_partitioner());

		// prepare exclusive sum array
		tbb::parallel_for(
			tbb::blocked_range<size_t>(0, pSumRes.size()),
			[&](tbb::blocked_range<size_t> const & r) {
				for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
					for (size_t offset = 0; offset < r.grainsize(); offset++) {
						auto i = grain + offset;

						pSumRes[i] = 0;
					}
				}
			},
			tbb::auto_partitioner());

		// set bit 1 which is left != right (not the case: left == right)
		std::atomic<uint64_t> ones;
		ones.store(0);
		tbb::parallel_for(
			tbb::blocked_range<size_t>(1, in->size()),
			[&](tbb::blocked_range<size_t> const & r) {
				uint64_t myOnes = 0;
				for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
					for (size_t offset = 0; offset < r.grainsize(); offset++) {
						auto prev = grain + offset - 1;
						auto curr = grain + offset;
						if (in->at(prev)[0] != in->at(curr)[0]) {
							setBit(curr);
							myOnes++;
						}
					}
				}
				ones.fetch_add(myOnes);
			},
			tbb::auto_partitioner());
		setBit(0);
		ones.fetch_add(1);

		// exclusive sum
		tbb::parallel_scan(
			tbb::blocked_range<size_t>(0, in->size()),
			0L, // very important! not be just '0'
			[&](tbb::blocked_range<size_t> const & r, uint64_t sum, bool isFinalScan) {
				auto temp = sum;
				for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
					for (size_t offset = 0; offset < r.grainsize(); offset++) {
						auto i = grain + offset;
						temp += (getBit(i) ? 1 : 0);
						if (isFinalScan) {
							pSumRes[i + 1] = temp;
						}
					}
				}
				return temp;
			},
			[&](size_t const & l, size_t const & r) { return l + r; },
			tbb::auto_partitioner());

		////////////////
		auto saveRow = std::thread([&] {
			std::vector<V32> out(ones);

			tbb::parallel_for(
				tbb::blocked_range<size_t>(0, in->size()),
				[&](tbb::blocked_range<size_t> const & r) {
					for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
						for (size_t offset = 0; offset < r.grainsize(); offset++) {
							auto i = grain + offset;

							if (getBit(i)) {
								out[pSumRes[i]] = in->at(i)[0];
							}
						}
					}
				},
				tbb::auto_partitioner());
			auto trueTarget = fs::path(outTarget.string() + ".row");
			fileSave(trueTarget, out.data(), out.size() * sizeof(V32));
		});

		auto savePtr = std::thread([&] {
			std::vector<V32> out(ones + 1);

			tbb::parallel_for(
				tbb::blocked_range<size_t>(0, in->size()),
				[&](tbb::blocked_range<size_t> const & r) {
					for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
						for (size_t offset = 0; offset < r.grainsize(); offset++) {
							auto i = grain + offset;

							if (getBit(i)) {
								out[pSumRes[i]] = i;
							}
						}
					}
				},
				tbb::auto_partitioner());
			out.back()		= in->size();
			auto trueTarget = fs::path(outTarget.string() + ".ptr");
			fileSave(trueTarget, out.data(), out.size() * sizeof(V32));
		});

		if (saveRow.joinable()) {
			saveRow.join();
		}

		if (savePtr.joinable()) {
			savePtr.join();
		}
	});

	auto fillCol = std::thread([&, in] {
		// fill col
		std::vector<V32> out(in->size());

		tbb::parallel_for(
			tbb::blocked_range<size_t>(0, in->size()),
			[&](tbb::blocked_range<size_t> const & r) {
				for (size_t grain = r.begin(); grain != r.end(); grain += r.grainsize()) {
					for (size_t offset = 0; offset < r.grainsize(); offset++) {
						auto i = grain + offset;

						out[i] = in->at(i)[1];
					}
				}
			},
			tbb::auto_partitioner());
		auto trueTarget = fs::path(outTarget.string() + ".col");
		fileSave(trueTarget, out.data(), out.size() * sizeof(V32));
	});

	if (fillCol.joinable()) {
		fillCol.join();
	}

	if (fillRowAndPtr.joinable()) {
		fillRowAndPtr.join();
	}
}

void stage2(fs::path const & inFolder, fs::path const & outFolder)
{
	auto jobs = [&] {
		auto out = makeSp<bchan<fs::path>>(16);
		std::thread([=] {
			auto fListChan = fileList(inFolder, ".el32");
			for (auto & f : *fListChan) {
				out->push(f);
			}
			out->close();
		}).detach();
		return out;
	}();

	parallelDo(8, [&](size_t const i) {
		for (auto & fPath : *jobs) {
			stopwatch("Stage2, " + std::string(fPath), [&] {
				auto rawData = fileLoad<E32>(fPath);
				auto deduped = dedup(rawData);
				auto target	 = fPath.parent_path() / fPath.stem();
				// auto sortedTarget = fs::path(fPath.string() + ".sort");
				// fileSave(sortedTarget, deduped->data(), deduped->size() * sizeof(E32));
				writeCSR(target, deduped);
				fs::remove(fPath);
			});
		}
	});
}