

void test() {
	u32 *array;
	u32 group_id;
	spustr_t *spu;
	sysSpuImage image;
	u32 cause,status,i;
	sysSpuThreadArgument arg[6];
	sysSpuThreadGroupAttribute grpattr = { 7+1, ptr2ea("mygroup"), 0, 0 };
	sysSpuThreadAttribute attr = { ptr2ea("mythread"), 8+1, SPU_THREAD_ATTR_NONE };

	TRACE_DEBUG("spuchain starting....\n");

	sysSpuInitialize( 6, 0 );
	sysSpuImageImport( &image,spu_spu_bin, 0 );
	sysSpuThreadGroupCreate( &group_id, 6, 100, &grpattr );

	spu = (spustr_t*)memalign( 128, 6*sizeof(spustr_t) );
	array = (u32*)memalign( 128, 4*sizeof(u32) );

	for(i=0;i<6;i++)
	{
		spu[i].rank		= i;
		spu[i].count	= 6;
		spu[i].sync		= 0;
		spu[i].array_ea	= ptr2ea(array);
		arg[i].arg0		= ptr2ea(&spu[i]);

		TRACE_DEBUG("Creating SPU thread...\n");
		sysSpuThreadInitialize(&spu[i].id,group_id,i,&image,&attr,&arg[i]);
		TRACE_DEBUG( "%08x\n", spu[i].id );
		sysSpuThreadSetConfiguration( spu[i].id,(SPU_SIGNAL1_OVERWRITE | SPU_SIGNAL2_OVERWRITE) );
	}

	TRACE_DEBUG("Starting SPU thread group....\n");
	sysSpuThreadGroupStart(group_id);

	TRACE_DEBUG("Initial array: ");

	for(i=0;i<4;i++) {
		array[i] = (i + 1);
		TRACE_DEBUG(" %d\n",array[i]);
	}

	/* Send signal notification to SPU 0 */
	TRACE_DEBUG( "sending signal.... \n" );
	sysSpuThreadWriteSignal( spu[0].id, 0, 1 );

	/* Wait for SPU 5 to return */
	while( spu[5].sync == 0 );

	TRACE_DEBUG("Output array: ");
	for(i=0;i<4;i++) TRACE_DEBUG(" %d\n",array[i]);

	TRACE_DEBUG("Joining SPU thread group....\n");
	sysSpuThreadGroupJoin(group_id,&cause,&status);
	sysSpuImageClose(&image);

	free(array);
	free(spu);
}

int ppu_init(int nb_spu) {
	//u32 *array;
	u32 group_id;
	spustr_t *spu;
	sysSpuImage image;
	u32 cause,status,i;
	sysSpuThreadArgument arg[6];
	sysSpuThreadGroupAttribute grpattr = { 7+1, ptr2ea("mygroup"), 0, 0 };
	sysSpuThreadAttribute attr = { ptr2ea("mythread"), 8+1, SPU_THREAD_ATTR_NONE };

	sysSpuInitialize( 6, 0 );
	sysSpuImageImport( &image,spu_spu_bin, 0 );
	sysSpuThreadGroupCreate( &group_id, nb_spu, 100, &grpattr );
}

int run_program() {
	
}