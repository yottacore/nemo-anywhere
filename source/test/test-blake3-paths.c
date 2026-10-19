/* Every one of blake3's routines that the build has, against blake3's own
 * published answers. Which one the app runs is picked at run time from what the
 * processor reports, so the checksum test only ever sees the one its own box
 * picks. Here blake3 is made to believe the processor has less than it does, one
 * routine at a time. A routine the processor can't run is skipped, and said so.
 *
 * The routines are the app's own libraries. Only the dispatcher and the code
 * around it are built again, by test-blake3-dispatch.c. */

#include <config.h>

#include <string.h>
#include <glib.h>

#include "blake3.h"

#include "test-blake3-dispatch.h"
#include "test-check.h"

#define XOF_LEN 131

static const char vector_key[] = "whats the Elvish word for friend";
static const char vector_context[] = "BLAKE3 2019-12-27 16:29:52 test vectors context";

/* From test_vectors.json at the vendored tag. The hash is the whole 131 bytes
 * given there, which reaches the extended output; the other two modes only
 * their first 32. */
static const struct {
	gsize       len;
	const char *hash;
	const char *keyed;
	const char *derived;
} vectors[] = {
	{ 0,
	  "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262e00f03e7b69af26b7faaf09fcd333050338ddfe085b8cc869ca98b206c08243a26f5487789e8f660afe6c99ef9e0c52b92e7393024a80459cf91f476f9ffdbda7001c22e159b402631f277ca96f2defdf1078282314e763699a31c5363165421cce14d",
	  "92b2b75604ed3c761f9d6f62392c8a9227ad0ea3f09573e783f1498a4ed60d26",
	  "2cc39783c223154fea8dfb7c1b1660f2ac2dcbd1c1de8277b0b0dd39b7e50d7d" },
	{ 1,
	  "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213c3a6cb8bf623e20cdb535f8d1a5ffb86342d9c0b64aca3bce1d31f60adfa137b358ad4d79f97b47c3d5e79f179df87a3b9776ef8325f8329886ba42f07fb138bb502f4081cbcec3195c5871e6c23e2cc97d3c69a613eba131e5f1351f3f1da786545e5",
	  "6d7878dfff2f485635d39013278ae14f1454b8c0a3a2d34bc1ab38228a80c95b",
	  "b3e2e340a117a499c6cf2398a19ee0d29cca2bb7404c73063382693bf66cb06c" },
	{ 64,
	  "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98fc9cc56cb831ffe33ea8e7e1d1df09b26efd2767670066aa82d023b1dfe8ab1b2b7fbb5b97592d46ffe3e05a6a9b592e2949c74160e4674301bc3f97e04903f8c6cf95b863174c33228924cdef7ae47559b10b294acd660666c4538833582b43f82d74",
	  "ba8ced36f327700d213f120b1a207a3b8c04330528586f414d09f2f7d9ccb7e6",
	  "a5c4a7053fa86b64746d4bb688d06ad1f02a18fce9afd3e818fefaa7126bf73e" },
	{ 65,
	  "de1e5fa0be70df6d2be8fffd0e99ceaa8eb6e8c93a63f2d8d1c30ecb6b263dee0e16e0a4749d6811dd1d6d1265c29729b1b75a9ac346cf93f0e1d7296dfcfd4313b3a227faaaaf7757cc95b4e87a49be3b8a270a12020233509b1c3632b3485eef309d0abc4a4a696c9decc6e90454b53b000f456a3f10079072baaf7a981653221f2c",
	  "c0a4edefa2d2accb9277c371ac12fcdbb52988a86edc54f0716e1591b4326e72",
	  "51fd05c3c1cfbc8ed67d139ad76f5cf8236cd2acd26627a30c104dfd9d3ff8a8" },
	{ 1024,
	  "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af71cf8107265ecdaf8505b95d8fcec83a98a6a96ea5109d2c179c47a387ffbb404756f6eeae7883b446b70ebb144527c2075ab8ab204c0086bb22b7c93d465efc57f8d917f0b385c6df265e77003b85102967486ed57db5c5ca170ba441427ed9afa684e",
	  "75c46f6f3d9eb4f55ecaaee480db732e6c2105546f1e675003687c31719c7ba4",
	  "7356cd7720d5b66b6d0697eb3177d9f8d73a4a5c5e968896eb6a689684302706" },
	{ 1025,
	  "d00278ae47eb27b34faecf67b4fe263f82d5412916c1ffd97c8cb7fb814b8444f4c4a22b4b399155358a994e52bf255de60035742ec71bd08ac275a1b51cc6bfe332b0ef84b409108cda080e6269ed4b3e2c3f7d722aa4cdc98d16deb554e5627be8f955c98e1d5f9565a9194cad0c4285f93700062d9595adb992ae68ff12800ab67a",
	  "357dc55de0c7e382c900fd6e320acc04146be01db6a8ce7210b7189bd664ea69",
	  "effaa245f065fbf82ac186839a249707c3bddf6d3fdda22d1b95a3c970379bcb" },
	{ 2049,
	  "5f4d72f40d7a5f82b15ca2b2e44b1de3c2ef86c426c95c1af0b687952256303096de31d71d74103403822a2e0bc1eb193e7aecc9643a76b7bbc0c9f9c52e8783aae98764ca468962b5c2ec92f0c74eb5448d519713e09413719431c802f948dd5d90425a4ecdadece9eb178d80f26efccae630734dff63340285adec2aed3b51073ad3",
	  "9f29700902f7c86e514ddc4df1e3049f258b2472b6dd5267f61bf13983b78dd5",
	  "2ea477c5515cc3dd606512ee72bb3e0e758cfae7232826f35fb98ca1bcbdf273" },
	{ 3073,
	  "7124b49501012f81cc7f11ca069ec9226cecb8a2c850cfe644e327d22d3e1cd39a27ae3b79d68d89da9bf25bc27139ae65a324918a5f9b7828181e52cf373c84f35b639b7fccbb985b6f2fa56aea0c18f531203497b8bbd3a07ceb5926f1cab74d14bd66486d9a91eba99059a98bd1cd25876b2af5a76c3e9eed554ed72ea952b603bf",
	  "68dede9bef00ba89e43f31a6825f4cf433389fedae75c04ee9f0cf16a427c95a",
	  "72613c9ec9ff7e40f8f5c173784c532ad852e827dba2bf85b2ab4b76f7079081" },
	{ 4097,
	  "9b4052b38f1c5fc8b1f9ff7ac7b27cd242487b3d890d15c96a1c25b8aa0fb99505f91b0b5600a11251652eacfa9497b31cd3c409ce2e45cfe6c0a016967316c426bd26f619eab5d70af9a418b845c608840390f361630bd497b1ab44019316357c61dbe091ce72fc16dc340ac3d6e009e050b3adac4b5b2c92e722cffdc46501531956",
	  "00df940cd36bb9fa7cbbc3556744e0dbc8191401afe70520ba292ee3ca80abbc",
	  "aca51029626b55fda7117b42a7c211f8c6e9ba4fe5b7a8ca922f34299500ead8" },
	{ 5121,
	  "628bd2cb2004694adaab7bbd778a25df25c47b9d4155a55f8fbd79f2fe154cff96adaab0613a6146cdaabe498c3a94e529d3fc1da2bd08edf54ed64d40dcd6777647eac51d8277d70219a9694334a68bc8f0f23e20b0ff70ada6f844542dfa32cd4204ca1846ef76d811cdb296f65e260227f477aa7aa008bac878f72257484f2b6c95",
	  "6ccf1c34753e7a044db80798ecd0782a8f76f33563accaddbfbb2e0ea4b2d024",
	  "b07f01e518e702f7ccb44a267e9e112d403a7b3f4883a47ffbed4b48339b3c34" },
	{ 6145,
	  "f1323a8631446cc50536a9f705ee5cb619424d46887f3c376c695b70e0f0507f18a2cfdd73c6e39dd75ce7c1c6e3ef238fd54465f053b25d21044ccb2093beb015015532b108313b5829c3621ce324b8e14229091b7c93f32db2e4e63126a377d2a63a3597997d4f1cba59309cb4af240ba70cebff9a23d5e3ff0cdae2cfd54e070022",
	  "9ac301e9e39e45e3250a7e3b3df701aa0fb6889fbd80eeecf28dbc6300fbc539",
	  "379bcc61d0051dd489f686c13de00d5b14c505245103dc040d9e4dd1facab8e5" },
	{ 7169,
	  "a003fc7a51754a9b3c7fae0367ab3d782dccf28855a03d435f8cfe74605e781798a8b20534be1ca9eb2ae2df3fae2ea60e48c6fb0b850b1385b5de0fe460dbe9d9f9b0d8db4435da75c601156df9d047f4ede008732eb17adc05d96180f8a73548522840779e6062d643b79478a6e8dbce68927f36ebf676ffa7d72d5f68f050b119c8",
	  "ed9b1a922c046fdb3d423ae34e143b05ca1bf28b710432857bf738bcedbfa511",
	  "554b0a5efea9ef183f2f9b931b7497995d9eb26f5c5c6dad2b97d62fc5ac31d9" },
	{ 8193,
	  "bab6c09cb8ce8cf459261398d2e7aef35700bf488116ceb94a36d0f5f1b7bc3bb2282aa69be089359ea1154b9a9286c4a56af4de975a9aa4a5c497654914d279bea60bb6d2cf7225a2fa0ff5ef56bbe4b149f3ed15860f78b4e2ad04e158e375c1e0c0b551cd7dfc82f1b155c11b6b3ed51ec9edb30d133653bb5709d1dbd55f4e1ff6",
	  "954a2a75420c8d6547e3ba5b98d963e6fa6491addc8c023189cc519821b4a1f5",
	  "af1e0346e389b17c23200270a64aa4e1ead98c61695d917de7d5b00491c9b0f1" },
	{ 16384,
	  "f875d6646de28985646f34ee13be9a576fd515f76b5b0a26bb324735041ddde49d764c270176e53e97bdffa58d549073f2c660be0e81293767ed4e4929f9ad34bbb39a529334c57c4a381ffd2a6d4bfdbf1482651b172aa883cc13408fa67758a3e47503f93f87720a3177325f7823251b85275f64636a8f1d599c2e49722f42e93893",
	  "9e9fc4eb7cf081ea7c47d1807790ed211bfec56aa25bb7037784c13c4b707b0d",
	  "160e18b5878cd0df1c3af85eb25a0db5344d43a6fbd7a8ef4ed98d0714c3f7e1" },
	{ 31744,
	  "62b6960e1a44bcc1eb1a611a8d6235b6b4b78f32e7abc4fb4c6cdcce94895c47860cc51f2b0c28a7b77304bd55fe73af663c02d3f52ea053ba43431ca5bab7bfea2f5e9d7121770d88f70ae9649ea713087d1914f7f312147e247f87eb2d4ffef0ac978bf7b6579d57d533355aa20b8b77b13fd09748728a5cc327a8ec470f4013226f",
	  "efa53b389ab67c593dba624d898d0f7353ab99e4ac9d42302ee64cbf9939a419",
	  "39772aef80e0ebe60596361e45b061e8f417429d529171b6764468c22928e28e" },
	{ 102400,
	  "bc3e3d41a1146b069abffad3c0d44860cf664390afce4d9661f7902e7943e085e01c59dab908c04c3342b816941a26d69c2605ebee5ec5291cc55e15b76146e6745f0601156c3596cb75065a9c57f35585a52e1ac70f69131c23d611ce11ee4ab1ec2c009012d236648e77be9295dd0426f29b764d65de58eb7d01dd42248204f45f8e",
	  "1c35d1a5811083fd7119f5d5d1ba027b4d01c0c6c49fb6ff2cf75393ea5db4a7",
	  "4652cff7a3f385a6103b5c260fc1593e13c778dbe608efb092fe7ee69df6e9c6" },
};

static guint8 *
vector_input (gsize len)
{
	guint8 *buf = g_malloc (len > 0 ? len : 1);
	gsize   i;

	for (i = 0; i < len; i++)
		buf[i] = (guint8) (i % 251);

	return buf;
}

/* Against the start of want, so a 32-byte answer can be read off the 131. */
static gboolean
same_hex (const guint8 *got, gsize len, const char *want)
{
	g_autoptr (GString) s = g_string_new (NULL);
	gsize i;

	for (i = 0; i < len; i++)
		g_string_append_printf (s, "%02x", got[i]);

	return strlen (want) >= s->len && memcmp (s->str, want, s->len) == 0;
}

/* %u rather than G_GSIZE_FORMAT, which cppcheck can't expand and then skips
 * the whole file over. */
static void
expect (const char *path, const char *what, guint n, const guint8 *got, gsize len, const char *want)
{
	gboolean same = same_hex (got, len, want);

	check (same);
	if (!same)
		g_printerr ("  %s, %s of %u bytes\n", path, what, (guint) vectors[n].len);
}

static void
check_vector (const char *path, guint n)
{
	g_autofree guint8 *in = vector_input (vectors[n].len);
	guint8 out[XOF_LEN];
	blake3_hasher hasher;
	gsize at, step;

	blake3_hasher_init (&hasher);
	blake3_hasher_update (&hasher, in, vectors[n].len);
	blake3_hasher_finalize (&hasher, out, XOF_LEN);
	expect (path, "hash", n, out, XOF_LEN, vectors[n].hash);

	/* A file is read in pieces, and a piece that ends partway into a chunk
	 * leaves the rest of it to the single-chunk code. */
	blake3_hasher_init (&hasher);
	for (at = 0; at < vectors[n].len; at += step) {
		step = MIN (4099, vectors[n].len - at);
		blake3_hasher_update (&hasher, in + at, step);
	}
	blake3_hasher_finalize (&hasher, out, BLAKE3_OUT_LEN);
	expect (path, "hash in pieces", n, out, BLAKE3_OUT_LEN, vectors[n].hash);

	blake3_hasher_init_keyed (&hasher, (const guint8 *) vector_key);
	blake3_hasher_update (&hasher, in, vectors[n].len);
	blake3_hasher_finalize (&hasher, out, BLAKE3_OUT_LEN);
	expect (path, "keyed hash", n, out, BLAKE3_OUT_LEN, vectors[n].keyed);

	blake3_hasher_init_derive_key (&hasher, vector_context);
	blake3_hasher_update (&hasher, in, vectors[n].len);
	blake3_hasher_finalize (&hasher, out, BLAKE3_OUT_LEN);
	expect (path, "derived key", n, out, BLAKE3_OUT_LEN, vectors[n].derived);
}

int
main (int argc, char *argv[])
{
	int    cpu;
	guint  ran = 0;
	gsize  p;

	(void) argc;
	(void) argv;

	cpu = test_blake3_cpu ();

	for (p = 0; p < test_blake3_n_paths; p++) {
		const TestBlake3Path *path = &test_blake3_paths[p];
		int    before = failures;
		guint  n;

		if (!path->built) {
			g_print ("%s: skipped, not in this build\n", path->name);
			continue;
		}
		if ((cpu & path->features) != path->features) {
			g_print ("%s: skipped, this processor can't run it\n", path->name);
			continue;
		}

		test_blake3_limit (path->features);

		/* Proves the limit took: a dispatcher that ignored it would run
		 * the newest routine every time and pass every row. */
		check (test_blake3_degree () == path->degree);

		for (n = 0; n < G_N_ELEMENTS (vectors); n++)
			check_vector (path->name, n);

		g_print ("%s: %s\n", path->name, failures == before ? "OK" : "FAILED");
		ran++;
	}

	test_blake3_limit (cpu);

	check (ran > 0);

	if (failures > 0) {
		g_printerr ("%d check(s) failed\n", failures);
		return 1;
	}

	g_print ("OK\n");
	return 0;
}
