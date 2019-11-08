#include "range_bintree.h"
# if 0
int main(int argc, char const *argv[])
{
	size_t i;
	struct range_bintree *rbt = range_bintree_init(10);

	for(i = 0; i < 500; i++){
		u64 v1 = 5000 + rand()%5000;
		u64 v2 = v1 + rand()%100;
		range_bintree_add(rbt, v1, v2);
	}
	range_bintree_optimize(rbt, RANGE_BINTREE_OPTLEVEL_MERGE);
	//range_bintree_optimize(rbt, RANGE_BINTREE_OPTLEVEL_SQUEEZE);
	for(i = 0; i < 100; i++){
		u64 v1;
		bool found;

		v1 = rand()%15000;
		//v1 = 4700;
		printf("lookup: %"PRIu64" = ", v1);
		fflush(stdout);
		found = range_bintree_find(rbt, v1);
		printf("%d\n", found);
	}
	range_bintree_print(rbt);
	return 0;
}
#endif