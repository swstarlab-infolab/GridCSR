#ifndef ECC8FE20_9095_45C9_AE8A_8627F57BD38F
#define ECC8FE20_9095_45C9_AE8A_8627F57BD38F

#include <array>
#include <string>

using XY  = std::array<size_t, 2>;
using XY3 = std::array<XY, 3>;

struct ShardIndex {
	XY			grid, shard;
	size_t		depth;
	std::string string() const;
	bool		parse(std::string const & in);
};

struct ShardRange {
	size_t			  depth;
	std::array<XY, 2> range; // [x_s,x_t),[y_s,y_t)
	void			  conv(ShardIndex const & in);
	bool			  increase(size_t const depth);
};
#endif /* ECC8FE20_9095_45C9_AE8A_8627F57BD38F */
