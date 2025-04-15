- When developing, configure with gcc warnings enabled:
e.g.
	cd ldms && ./autogen.sh
	mkdir obj && cd obj
	CFLAGS="-Wall -g" ../configure --prefix=$someplace
	make -j
	cd obj/ldms/src/sampler/gpfs\_sampler
	make clean; make
	# edit file in ../../../../../ldms/src/sampler/gpfs\_sampler/gpfs... and repeat make clean;make as needed
	# when ready to test, make install from the obj.....gpfs\_sampler directory to reinstall plugin only.

- I fixed the includes and made a header for gpfs\_parse.c so it
  will build without including a .c file.
- I stripped ^M chars with dos2unix.
- I added fs\_io\_s.test.input which can be used in to test the parse routine.
- Move gpfs\_parse.c:main to  test\_parse.c
- Add an installcheck rule to Makefile.am which tests your parser main with the input file. See https://www.gnu.org/software/automake/manual/html_node/Install-Tests.html
- gpfs.h needs the struct vertically formatted, one member per line.
- gpfs.h needs the copyright notice inserted (clone from gpfs.c).
- gpfs\_parse needs switch case kernel formatted; see https://www.kernel.org/doc/html/v4.10/process/coding-style.html
- More generally, see https://docs.kernel.org/dev-tools/clang-format.html, for automation. clang-format is available on
  the clusters.
- In all files, update copyright date to 2025, not 2021, for new code.
- Update SAMP macro from gpfs to gpfs\_sampler, since that's the library name created in Makefile.am/seen by user.
- Avoid magic numbers (int constants/string constants that are arguments or that influence array sizes), e.g.:
  struct gpfs { name[20], ..} should be something like gpfs {name[MAX\_DEVICE\_NAME\_SIZE], ... } where
  you previously #define MAX\_DEVICE\_NAME\_SIZE 128 and cite origin if possible. In this case:
	https://www.ibm.com/docs/en/storage-scale/5.2.2?topic=considerations-device-name-file-system
  We know urls bit-rot, but it gives the maintainer a fighting chance in the future.
  Where an array size (e.g. hostname len max) is a constant available from another header,
  ensure that macro and its header of origin is used.
- Verify and remove unused variables mentioned by gcc.
- Verify and remove unused labels mentioned by gcc.
- Where do we attach to /usr/lpp/mmfs/bin/mmpmon in the code to fetch a line to parse?
    or alternately, where do we call a dummy function to return a test string?
- Where do we call gpfs\_set to parse the string into the struct?
- Should probably rename gpfs\_set to something like gpfs\_parse\_fs\_io\_s
  since there are several line and multiline formats to deal with eventually.
- Lines around 420 look like it was to be replaced with gpfs\_set.
