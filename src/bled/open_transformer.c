/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

void FAST_FUNC init_transformer_state(transformer_state_t *xstate)
{
	memset(xstate, 0, sizeof(*xstate));
}

int FAST_FUNC check_signature16(transformer_state_t *xstate, unsigned magic16)
{
	if (!xstate->signature_skipped) {
		uint16_t magic2;
		if (full_read(xstate->src_fd, &magic2, 2) != 2 || magic2 != magic16) {
			bb_error_msg("invalid magic");
			return -1;
		}
		xstate->signature_skipped = 2;
	}
	return 0;
}

ssize_t FAST_FUNC transformer_write(transformer_state_t *xstate, const void *buf, size_t bufsize)
{
	ssize_t nwrote;

	if (xstate->mem_output_size_max != 0) {
		size_t pos = xstate->mem_output_size;
		nwrote = bufsize;
		if ((pos + bufsize) > xstate->mem_output_size_max) {
			bufsize = xstate->mem_output_size_max - pos;
			// Use ENOSPC as an indicator that our buffer is full
			nwrote = -ENOSPC;
		}
		memcpy(xstate->mem_output_buf + pos, buf, bufsize);
		xstate->mem_output_size += bufsize;
	} else {
		nwrote = full_write(xstate->dst_fd, buf, (unsigned int)bufsize);
		if (nwrote != (ssize_t)bufsize) {
			if (nwrote < 0)
				bb_perror_msg("write error: %d", (int)nwrote);
			else
				bb_perror_msg("write error: %d bytes written but %d expected", (int)nwrote, (int)bufsize);
			nwrote = -1;
			goto ret;
		}
	}
 ret:
	return nwrote;
}

ssize_t FAST_FUNC xtransformer_write(transformer_state_t *xstate, const void *buf, size_t bufsize)
{
	ssize_t nwrote = transformer_write(xstate, buf, bufsize);
	if (nwrote != (ssize_t)bufsize) {
		xfunc_die();
	}
	return nwrote;
}

void check_errors_in_children(int signo)
{
	int status;

	if (!signo) {
		/* block waiting for any child */
		if (wait(&status) < 0)
//FIXME: check EINTR?
			return; /* probably there are no children */
		goto check_status;
	}

	/* Wait for any child without blocking */
	for (;;) {
		if (wait_any_nohang(&status) < 0)
//FIXME: check EINTR?
			/* wait failed?! I'm confused... */
			return;
 check_status:
		/*if (WIFEXITED(status) && WEXITSTATUS(status) == 0)*/
		/* On Linux, the above can be checked simply as: */
		if (status == 0)
			/* this child exited with 0 */
			continue;
		/* Cannot happen:
		if (!WIFSIGNALED(status) && !WIFEXITED(status)) ???;
		 */
		bb_got_signal = 1;
	}
}

#if SEAMLESS_COMPRESSION

/* transformer(), more than meets the eye */
#if BB_MMU
void FAST_FUNC fork_transformer(int fd,
	int signature_skipped,
	IF_DESKTOP(long long) int FAST_FUNC (*transformer)(transformer_state_t *xstate)
)
#else
void FAST_FUNC fork_transformer(int fd, const char *transform_prog)
#endif
{
	struct fd_pair fd_pipe;
	int pid;

	xpiped_pair(fd_pipe);
	pid = BB_MMU ? xfork() : xvfork();
	if (pid == 0) {
		/* Child */
		_close(fd_pipe.rd); /* we don't want to read from the parent */
		// FIXME: error check?
#if BB_MMU
		{
			IF_DESKTOP(long long) int r;
			transformer_state_t xstate;
			init_transformer_state(&xstate);
			xstate.signature_skipped = signature_skipped;
			xstate.src_fd = fd;
			xstate.dst_fd = fd_pipe.wr;
			r = transformer(&xstate);
			if (ENABLE_FEATURE_CLEAN_UP) {
				_close(fd_pipe.wr); /* send EOF */
				_close(fd);
			}
			/* must be _exit! bug was actually seen here */
			_exit(/*error if:*/ r < 0);
		}
#else
		{
			char *argv[4];
			xmove_fd(fd, 0);
			xmove_fd(fd_pipe.wr, 1);
			argv[0] = (char*)transform_prog;
			argv[1] = (char*)"-cf";
			argv[2] = (char*)"-";
			argv[3] = NULL;
			BB_EXECVP(transform_prog, argv);
			bb_perror_msg_and_die("can't execute '%s'", transform_prog);
		}
#endif
		/* notreached */
	}

	/* parent process */
	_close(fd_pipe.wr); /* don't want to write to the child */
	xmove_fd(fd_pipe.rd, fd);
}

/* Used by e.g. rpm which gives us a fd without filename,
 * thus we can't guess the format from filename's extension.
 */
static transformer_state_t *setup_transformer_on_fd(int fd, int fail_if_not_compressed)
{
	transformer_state_t *xstate;

	xstate = xzalloc(sizeof(*xstate));
	xstate->src_fd = fd;

	/* .gz and .bz2 both have 2-byte signature, and their
	 * unpack_XXX_stream wants this header skipped. */
	xstate->signature_skipped = 2;
	xread(fd, xstate->magic.b16, 2);
	if (ENABLE_FEATURE_SEAMLESS_GZ
	 && xstate->magic.b16[0] == GZIP_MAGIC
	) {
		xstate->xformer = unpack_gz_stream;
		USE_FOR_NOMMU(xstate->xformer_prog = "gunzip";)
		goto found_magic;
	}
	if (ENABLE_FEATURE_SEAMLESS_Z
	 && xstate->magic.b16[0] == COMPRESS_MAGIC
	) {
		xstate->xformer = unpack_Z_stream;
		USE_FOR_NOMMU(xstate->xformer_prog = "uncompress";)
		goto found_magic;
	}
	if (ENABLE_FEATURE_SEAMLESS_BZ2
	 && xstate->magic.b16[0] == BZIP2_MAGIC
	) {
		xstate->xformer = unpack_bz2_stream;
		USE_FOR_NOMMU(xstate->xformer_prog = "bunzip2";)
		goto found_magic;
	}
	if (ENABLE_FEATURE_SEAMLESS_XZ
	 && xstate->magic.b16[0] == XZ_MAGIC1
	) {
		uint32_t v32;
		xstate->signature_skipped = 6;
		xread(fd, &xstate->magic.b16[1], 4);
		move_from_unaligned32(v32, &xstate->magic.b16[1]);
		if (v32 == XZ_MAGIC2) {
			xstate->xformer = unpack_xz_stream;
			USE_FOR_NOMMU(xstate->xformer_prog = "unxz";)
			goto found_magic;
		}
	}
	if (ENABLE_FEATURE_SEAMLESS_ZSTD
	 && xstate->magic.b16[0] == ZSTD_MAGIC1
	) {
		uint16_t v16;
		xstate->signature_skipped = 4;
		xread(fd, &xstate->magic.b16[1], 2);
		move_from_unaligned16(v16, &xstate->magic.b16[1]);
		if (v16 == ZSTD_MAGIC2) {
			xstate->xformer = unpack_zstd_stream;
			USE_FOR_NOMMU(xstate->xformer_prog = "unzstd";)
			goto found_magic;
		}
	}

	/* No known magic seen */
	if (fail_if_not_compressed)
		bb_error_msg_and_die("no gzip"
			IF_FEATURE_SEAMLESS_BZ2("/bzip2")
			IF_FEATURE_SEAMLESS_XZ("/xz")
			IF_FEATURE_SEAMLESS_ZSTD("/zstd")
			" magic");

	/* Some callers expect this function to "consume" fd
	 * even if data is not compressed. In this case,
	 * we return a state with trivial transformer.
	 */
//	USE_FOR_MMU(xstate->xformer = copy_stream;)
//	USE_FOR_NOMMU(xstate->xformer_prog = "cat";)

 found_magic:
	return xstate;
}

static void fork_transformer_and_free(transformer_state_t *xstate)
{
# if BB_MMU
	fork_transformer_with_no_sig(xstate->src_fd, xstate->xformer);
# else
	/* NOMMU version of fork_transformer execs
	 * an external unzipper that wants
	 * file position at the start of the file.
	 */
	xlseek(xstate->src_fd, - xstate->signature_skipped, SEEK_CUR);
	xstate->signature_skipped = 0;
	fork_transformer_with_sig(xstate->src_fd, xstate->xformer, xstate->xformer_prog);
# endif
	free(xstate);
}

/* Used by e.g. rpm which gives us a fd without filename,
 * thus we can't guess the format from filename's extension.
 */
int FAST_FUNC setup_unzip_on_fd(int fd, int fail_if_not_compressed)
{
	transformer_state_t *xstate = setup_transformer_on_fd(fd, fail_if_not_compressed);

	if (!xstate || !xstate->xformer) {
		free(xstate);
		return 1;
	}

	fork_transformer_and_free(xstate);
	return 0;
}
#if ENABLE_FEATURE_SEAMLESS_LZMA
/* ...and custom version for LZMA */
void FAST_FUNC setup_lzma_on_fd(int fd)
{
	transformer_state_t *xstate = xzalloc(sizeof(*xstate));
	xstate->src_fd = fd;
	xstate->xformer = unpack_lzma_stream;
	USE_FOR_NOMMU(xstate->xformer_prog = "unlzma";)
	fork_transformer_and_free(xstate);
}
#endif

static transformer_state_t *open_transformer(const char *fname, int fail_if_not_compressed)
{
	transformer_state_t *xstate;
	int fd;

	fd = _open(fname, O_RDONLY);
	if (fd < 0)
		return NULL;

	if (ENABLE_FEATURE_SEAMLESS_LZMA) {
		/* .lzma has no header/signature, can only detect it by extension */
		char *sfx = strrchr(fname, '.');
		if (sfx && strcmp(sfx+1, "lzma") == 0) {
			xstate = xzalloc(sizeof(*xstate));
			xstate->src_fd = fd;
			xstate->xformer = unpack_lzma_stream;
			USE_FOR_NOMMU(xstate->xformer_prog = "unlzma";)
			return xstate;
		}
	}

	xstate = setup_transformer_on_fd(fd, fail_if_not_compressed);

	return xstate;
}

int FAST_FUNC open_zipped(const char *fname, int fail_if_not_compressed)
{
	int fd;
	transformer_state_t *xstate;

	xstate = open_transformer(fname, fail_if_not_compressed);
	if (!xstate)
		return -1;

	fd = xstate->src_fd;
# if BB_MMU
	if (xstate->xformer) {
		fork_transformer_with_no_sig(fd, xstate->xformer);
	} else {
		/* the file is not compressed */
		xlseek(fd, - xstate->signature_skipped, SEEK_CUR);
		xstate->signature_skipped = 0;
	}
# else
	/* NOMMU can't avoid the seek :( */
	xlseek(fd, - xstate->signature_skipped, SEEK_CUR);
	xstate->signature_skipped = 0;
	if (xstate->xformer) {
		fork_transformer_with_sig(fd, xstate->xformer, xstate->xformer_prog);
	} /* else: the file is not compressed */
# endif

	free(xstate);
	return fd;
}

void* FAST_FUNC xmalloc_open_zipped_read_close(const char *fname, size_t *maxsz_p)
{
# if 1
	transformer_state_t *xstate;
	char *image;

	xstate = open_transformer(fname, /*fail_if_not_compressed:*/ 0);
	if (!xstate) /* file open error */
		return NULL;

	image = NULL;
	if (xstate->xformer) {
		/* In-memory decompression */
		xstate->mem_output_size_max = maxsz_p ? *maxsz_p : (size_t)(INT_MAX - 4095);
		xstate->xformer(xstate);
		if (xstate->mem_output_buf) {
			image = xstate->mem_output_buf;
			if (maxsz_p)
				*maxsz_p = xstate->mem_output_size;
		}
	} else {
		/* File is not compressed.
		 * We already read first few bytes, account for that.
		 * Example where it happens:
		 * "modinfo MODULE.ko" (not compressed)
		 *   open("MODULE.ko", O_RDONLY|O_LARGEFILE) = 4
		 *   read(4, "\177E", 2)                     = 2
		 *   fstat64(4, ...)
		 *   mmap(...)
		 *   read(4, "LF\2\1\1\0\0\0\0"...
		 * ...and we avoided seeking on the fd! :)
		 */
		image = xmalloc_read_with_initial_buf(
			xstate->src_fd,
			maxsz_p,
			xmemdup(&xstate->magic, xstate->signature_skipped),
			xstate->signature_skipped
		);
		xstate->signature_skipped = 0;
	}

	if (!image)
		bb_perror_msg("read error from '%s'", fname);
	_close(xstate->src_fd);
	free(xstate);
	return image;
# else
	/* This version forks a subprocess - much more expensive */
	int fd;
	char *image;

	fd = open_zipped(fname, /*fail_if_not_compressed:*/ 0);
	if (fd < 0)
		return NULL;

	image = xmalloc_read(fd, maxsz_p);
	if (!image)
		bb_perror_msg("read error from '%s'", fname);
	close(fd);
	return image;
# endif
}

#endif /* SEAMLESS_COMPRESSION */
