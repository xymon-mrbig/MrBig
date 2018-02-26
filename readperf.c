#include "mrbig.h"

#define DEFAULT_BUFFER_SIZE 4096

struct counter_info {
	DWORD size, offset;
};

/*
struct perfcounter *read_perfcounters(DWORD object, DWORD *counters)

If no value can be read, the function returns NULL.

If there are no instances, the function returns a single struct
perfcounter where the instance field is NULL.

If there are instances, the function returns an array of struct
perfcounter where each element has the instance name in the
instance field. The end of the struct is marked by an element
with a NULL instance field.

This seems to solve the general case completely. I can write
special versions of the function if it seems worthwhile.
*/

static PERF_OBJECT_TYPE *first_object(PERF_DATA_BLOCK *data_block)
{
	return (PERF_OBJECT_TYPE *)((BYTE *)data_block+data_block->HeaderLength);
}

static PERF_OBJECT_TYPE *next_object(PERF_OBJECT_TYPE *act)
{
	return (PERF_OBJECT_TYPE *)((BYTE *)act+act->TotalByteLength);
}

static PERF_COUNTER_DEFINITION *first_counter(PERF_OBJECT_TYPE *perf_object)
{
	return (PERF_COUNTER_DEFINITION *)((BYTE *)perf_object+perf_object->HeaderLength);
}

static PERF_COUNTER_DEFINITION *next_counter(PERF_COUNTER_DEFINITION *perf_counter)
{
	return (PERF_COUNTER_DEFINITION *)((BYTE *)perf_counter+perf_counter->ByteLength);
}

static PERF_COUNTER_BLOCK *get_counter_block(PERF_INSTANCE_DEFINITION *p_instance)
{
	return (PERF_COUNTER_BLOCK *)((BYTE *)p_instance+p_instance->ByteLength);
}

static PERF_INSTANCE_DEFINITION *first_instance(PERF_OBJECT_TYPE *p_object)
{
	return (PERF_INSTANCE_DEFINITION *)((BYTE *)p_object+p_object->DefinitionLength);
}

static PERF_INSTANCE_DEFINITION *next_instance(PERF_INSTANCE_DEFINITION *p_instance)
{
	/* next instance is after this instance + counter data */
	PERF_COUNTER_BLOCK *p_ctr_blk = get_counter_block(p_instance);
	return (PERF_INSTANCE_DEFINITION *)((BYTE *)p_instance+p_instance->ByteLength+p_ctr_blk->ByteLength);
}

/* Get size and offset of a counter. Return 1 for success, 0 for failure */
/* If counter is odd, use the next index. This is for the incredibly
   boneheaded way fractions are handled by the performance counters. */
static int get_counter_offset(PERF_OBJECT_TYPE *op, int counter,
				struct counter_info *ci)
{
	int b;
	PERF_COUNTER_DEFINITION *cd;
	int base = counter & 1;

	counter &= ~1;

	cd = first_counter(op);
	for (b = 0; b < op->NumCounters; b++) {
		if (cd->CounterNameTitleIndex == counter) {
			if (base) {
				base = 0;	/* use the next if any */
			} else {
				ci->size = cd->CounterSize;
				ci->offset = cd->CounterOffset;
				return 1;
			}
		}
		cd = next_counter(cd);
	}
	return 0;
}

static PERF_OBJECT_TYPE *get_object(PERF_DATA_BLOCK *bp, int object)
{
	int a;
	PERF_OBJECT_TYPE *op;

	if (debug > 1) mrlog("get_object(%p, %d)", bp, object);

	op = first_object(bp);
	for (a = 0; a < bp->NumObjectTypes; a++) {
		if (debug > 1) mrlog("object %d is %d", a,
					op->ObjectNameTitleIndex);
		if (op->ObjectNameTitleIndex == object) return op;
		op = next_object(op);
	}
	return NULL;
}

static char *dup_wide_to_multi(wchar_t *source)
{
	char b[1024];
	WideCharToMultiByte(CP_ACP, 0, source, -1, b, sizeof b, 0, 0);
	b[(sizeof b)-1] = '\0';
	return big_strdup("dup_wide_to_multi", b);
}

struct perfcounter *read_perfcounters(DWORD object, DWORD *counters,
			long long *perf_time, long long *perf_freq)
{
	int i, ncounters, b;
	char obj[100];
	void *data = big_malloc("read_perfcounters (data)", DEFAULT_BUFFER_SIZE);
	DWORD size = DEFAULT_BUFFER_SIZE;
	DWORD ret;
	struct counter_info *ci;
	PERF_OBJECT_TYPE *object_ptr;
	PERF_COUNTER_BLOCK *counter_block_ptr;
	PERF_INSTANCE_DEFINITION *instance_ptr;
	DWORD type;
	wchar_t *name_ptr;
	struct perfcounter *results;

	if (debug) mrlog("read_perfcounters(object = %ld)", (long)object);

	/* check to see how many counters we are interested in */
	for (ncounters = 0; counters[ncounters]; ncounters++) {
		if (debug > 1) mrlog("counters[%d] = %ld",
			ncounters, (long)counters[ncounters]);
	}

	if (debug > 1) mrlog("%d interesting counters", ncounters);

	obj[0] = '\0';
	snprcat(obj, sizeof obj, "%ld", (long)object);
	if (debug > 2) mrlog("About to call RegQueryValueEx");
	ret = RegQueryValueEx(HKEY_PERFORMANCE_DATA, obj, 0, &type,
			(BYTE *)data, &size);
	if (debug > 2) mrlog("RegQueryValueEx returned %ld", ret);
	while (ret != ERROR_SUCCESS) {
		if (ret == ERROR_MORE_DATA) {
			if (debug > 1) mrlog("Increase buffer");
			size += DEFAULT_BUFFER_SIZE;
			data = big_realloc("read_perfcounters (data)", data, size);
		} else {
			if (debug) mrlog("Giving up");
			big_free("read_perfcounters (data)", data);
			return NULL;
		}
		ret = RegQueryValueEx(HKEY_PERFORMANCE_DATA, obj, 0, &type,
				(BYTE *)data, &size);
	}
	if (debug > 1) mrlog("RegQueryValueEx returned ERROR_SUCCESS");
	object_ptr = get_object(data, object);
	if (debug > 2) mrlog("get_object returned %p", object_ptr);
	if (object_ptr == NULL) {
		mrlog("Can't get object %d, giving up", object);
		big_free("read_perfcounters (data)", data);
		return NULL;
	}
	if (perf_time) *perf_time = object_ptr->PerfTime.QuadPart;
	if (perf_freq) *perf_freq = object_ptr->PerfFreq.QuadPart;
	ci = big_malloc("read_perfcounters (ci)", ncounters * sizeof *ci);
	for (i = 0; i < ncounters; i++) {
		get_counter_offset(object_ptr, counters[i], ci+i);
	}

	if (object_ptr->NumInstances == PERF_NO_INSTANCES) {
		if (debug > 1) mrlog("No instances");
		results = big_malloc("read_perfcounters (results)",
				sizeof *results);
		results[0].instance = NULL;
		results[0].value = big_malloc("read_perfcounters (value)",
				ncounters * sizeof *results[0].value);
		counter_block_ptr = (PERF_COUNTER_BLOCK *)
			((BYTE *)object_ptr+object_ptr->DefinitionLength);
		for (i = 0; i < ncounters; i++) {
			memset(&(results[0].value[i]),
				sizeof results[b].value[i],
				0);
			memcpy(&(results[0].value[i]),
				(BYTE *)counter_block_ptr+ci[i].offset,
				ci[i].size);
		}
		goto Done;
	}

	results = big_malloc("read_perfcounter (results)",
			(1+object_ptr->NumInstances) * sizeof *results);
	instance_ptr = first_instance(object_ptr);
	for (b = 0; b < object_ptr->NumInstances; b++) {
		results[b].value = big_malloc("read_perfcounter (value)",
				ncounters * sizeof *results[b].value);
		name_ptr = (wchar_t *)
			((BYTE *)instance_ptr+instance_ptr->NameOffset);
		counter_block_ptr = get_counter_block(instance_ptr);
		results[b].instance = dup_wide_to_multi(name_ptr);
		for (i = 0; i < ncounters; i++) {
			memset(&(results[b].value[i]),
				sizeof results[b].value[i],
				0);
			memcpy(&(results[b].value[i]),
				(BYTE *)counter_block_ptr+ci[i].offset,
				ci[i].size);
		}
		instance_ptr = next_instance(instance_ptr);
	}
	results[b].instance = NULL;

Done:
	if (debug > 1) mrlog("read_perfcounters returns %p", results);
	big_free("read_perfcounters (data)", data);
	big_free("read_perfcounters (ci)", ci);
	return results;
}

void free_perfcounters(struct perfcounter *pc)
{
	int i;

	if (debug > 1) mrlog("free_perfcounters(%p)", pc);

	if (pc == NULL) return;

	if (pc[0].instance == NULL) {
		big_free("free_perfcounters (value)", pc[0].value);
		big_free("free_perfcounters (table)", pc);
		return;
	}

	for (i = 0; pc[i].instance; i++) {
		big_free("free_perfcounters (instance)", pc[i].instance);
		big_free("free_perfcounters (value)", pc[i].value);
	}
	big_free("free_perfcounters (table)", pc);
	return;
}

void print_perfcounters(struct perfcounter *pc, int ncounters)
{
	int i, j;

	if (debug > 1) mrlog("print_perfcounters(%p, %d)", pc, ncounters);

	if (pc == NULL) {
		printf("No counters\n");
		return;
	}

	if (pc[0].instance == NULL) {
		printf("No instances. Values:\n");
		for (j = 0; j < ncounters; j++) {
			printf("%d\t%ld\n", j, (long)pc[0].value[j]);
		}
		return;
	}

	for (i = 0; pc[i].instance; i++) {
		printf("Instance '%s':\n", pc[i].instance);
		for (j = 0; j < ncounters; j++) {
			printf("%d\t%ld\n", j, (long)pc[i].value[j]);
		}
	}
}

#if 0
int main(int argc, char **argv)
{
	DWORD object = 238;
	DWORD counters[] = {6, 0};	/* ID */
	struct perfcounter *pc;
	long long perf_time, perf_freq;
	long long t0, t1, ut0, ut1, pt0, pt1;
	int i;
	double pct;

	pc = read_perfcounters(object, counters, &perf_time, &perf_freq);
	for (i = 0; pc[i].instance; i++) {
		if (!strcmp(pc[i].instance, "_Total")) break;
	}
	if (pc[i].instance == NULL) {
		printf("No data found\n");
		return EXIT_FAILURE;
	}
	t0 = pc[i].value[0];
	ut0 = pc[i].value[1];
	pt0 = pc[i].value[2];
	free_perfcounters(pc);
	sleep(2);
	pc = read_perfcounters(object, counters, &perf_time, &perf_freq);
	for (i = 0; pc[i].instance; i++) {
		if (!strcmp(pc[i].instance, "_Total")) break;
	}
	if (pc[i].instance == NULL) {
		printf("No data found\n");
		return EXIT_FAILURE;
	}
	t1 = pc[i].value[0];
	ut1 = pc[i].value[1];
	pt1 = pc[i].value[2];
	printf("Processor time: %ld\n", (long)(t1-t0));
	//printf("User time: %ld\n", (long)(ut1-ut0));
	//printf("Priv time: %ld\n", (long)(pt1-pt0));
	pct = 20000000-(t1-t0);
	printf("Load: %.2f%%\n", pct/10000);
	free_perfcounters(pc);
	return 0;
}
#endif
#if 0
int main(int argc, char **argv)
{
	DWORD object = 230;
	DWORD counters[] = {784, 6, 142, 144, 0};	/* ID */
	long long perf_time, perf_freq;
	struct perfcounter *pc = read_perfcounters(object, counters,
						&perf_time, &perf_freq);
	print_perfcounters(pc, 4);
	printf("Uptime = %ld\n", (long)((perf_time-pc[0].value[0])/perf_freq));
	free_perfcounters(pc);
	return 0;
}
#endif
