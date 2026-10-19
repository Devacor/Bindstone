"""Certificates for Bindstone's private TLS links (lobby, game servers), issued by one private CA.

    python Tools/LocalServer/make_certs.py                         # make whatever is missing or expires within 14 days
    python Tools/LocalServer/make_certs.py lobby [--host H ...]    # reissue the lobby certificate
    python Tools/LocalServer/make_certs.py gameserver NAME [--host H ...]   # reissue a game server's pair
    python Tools/LocalServer/make_certs.py ca                      # a NEW CA: every other certificate must be reissued
    python Tools/LocalServer/make_certs.py --list                  # what exists and when it expires

Output (default <repo>/Certificates/Tls, gitignored; --out DIR elsewhere):
    ca.pem, ca.key                          the CA; every process trusts ca.pem, ca.key signs the rest
    lobby.pem, lobby.key                    the lobby's server certificate (ports 22325 and 22326)
    gameservers/NAME/server.pem, .key       what players verify when they join that game server
    gameservers/NAME/client.pem, .key       what the lobby verifies when that game server registers

Hosts (--host, repeatable) become the certificate's subjectAltNames; localhost, 127.0.0.1 and ::1 are always
included. Keys are ECDSA P-256; the CA lasts 5 years, server and client certificates 90 days.
Needs only Python and an openssl executable (Git for Windows ships one; found on PATH or in its usual folders).
"""
import argparse, datetime, ipaddress, os, secrets, shutil, subprocess, sys, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_OUT = os.path.join(ROOT, "Certificates", "Tls")
LOCAL_HOSTS = ["localhost", "127.0.0.1", "::1"]
CA_DAYS = 1825
LEAF_DAYS = 90
RENEW_WITHIN_DAYS = 14


def find_openssl():
	candidates = [shutil.which("openssl")]
	for base in [os.environ.get("ProgramFiles", r"C:\Program Files"), os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs")]:
		candidates += [os.path.join(base, "Git", "mingw64", "bin", "openssl.exe"), os.path.join(base, "Git", "usr", "bin", "openssl.exe")]
	for candidate in candidates:
		if candidate and os.path.isfile(candidate):
			return candidate
	sys.exit("make_certs: no openssl executable found (install Git for Windows or put openssl on PATH)")


OPENSSL = None


def openssl(*args):
	env = dict(os.environ, MSYS_NO_PATHCONV="1", MSYS2_ARG_CONV_EXCL="*")
	result = subprocess.run([OPENSSL, *args], capture_output=True, text=True, env=env)
	if result.returncode != 0:
		sys.exit("make_certs: openssl " + " ".join(args[:2]) + " failed:\n" + result.stderr)
	return result.stdout


def subject_alt_names(hosts):
	names = []
	for host in dict.fromkeys(LOCAL_HOSTS + list(hosts)):
		try:
			ipaddress.ip_address(host)
			names.append("IP:" + host)
		except ValueError:
			names.append("DNS:" + host)
	return ",".join(names)


def write_config(directory, common_name, extensions):
	path = os.path.join(directory, "openssl.cnf")
	with open(path, "w") as config:
		config.write("[req]\nprompt = no\ndistinguished_name = dn\n\n")
		config.write("[dn]\nO = Bindstone\nCN = " + common_name + "\n\n")
		config.write("[ext]\n" + "\n".join(extensions) + "\n")
	return path


def make_key(path):
	openssl("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", path)


def make_ca(out):
	key, cert = os.path.join(out, "ca.key"), os.path.join(out, "ca.pem")
	with tempfile.TemporaryDirectory() as scratch:
		config = write_config(scratch, "Bindstone Private CA", ["basicConstraints = critical, CA:TRUE, pathlen:0", "keyUsage = critical, keyCertSign, cRLSign", "subjectKeyIdentifier = hash"])
		make_key(key)
		openssl("req", "-x509", "-new", "-key", key, "-sha256", "-days", str(CA_DAYS), "-config", config, "-extensions", "ext", "-set_serial", "0x" + secrets.token_hex(16), "-out", cert)
	print("[make_certs] CA: " + cert)


def make_leaf(out, stem, common_name, purpose, hosts):
	ca_key, ca_cert = os.path.join(out, "ca.key"), os.path.join(out, "ca.pem")
	if not os.path.isfile(ca_key):
		sys.exit("make_certs: " + ca_key + " is missing; run `make_certs.py ca` first (a new CA means reissuing everything)")
	key, cert = stem + ".key", stem + ".pem"
	os.makedirs(os.path.dirname(stem), exist_ok=True)
	with tempfile.TemporaryDirectory() as scratch:
		config = write_config(scratch, common_name, [
			"basicConstraints = critical, CA:FALSE",
			"keyUsage = critical, digitalSignature",
			"extendedKeyUsage = " + purpose,
			"subjectAltName = " + subject_alt_names(hosts),
			"subjectKeyIdentifier = hash",
			"authorityKeyIdentifier = keyid",
		])
		request = os.path.join(scratch, "request.csr")
		make_key(key)
		openssl("req", "-new", "-key", key, "-config", config, "-out", request)
		openssl("x509", "-req", "-in", request, "-CA", ca_cert, "-CAkey", ca_key, "-set_serial", "0x" + secrets.token_hex(16), "-days", str(LEAF_DAYS), "-sha256", "-extfile", config, "-extensions", "ext", "-out", cert)
	print("[make_certs] " + common_name + " (" + purpose + "): " + cert + "  [" + subject_alt_names(hosts) + "]")


def make_lobby(out, hosts):
	make_leaf(out, os.path.join(out, "lobby"), "Bindstone Lobby", "serverAuth", hosts)


def make_gameserver(out, name, hosts):
	if not name or any(c in name for c in "/\\:. "):
		sys.exit("make_certs: a game server name is one word (letters, digits, - or _)")
	folder = os.path.join(out, "gameservers", name)
	make_leaf(out, os.path.join(folder, "server"), "Bindstone Game Server " + name, "serverAuth", hosts)
	make_leaf(out, os.path.join(folder, "client"), "gameserver:" + name, "clientAuth", hosts)


def expiry(cert):
	if not os.path.isfile(cert):
		return None
	line = openssl("x509", "-in", cert, "-noout", "-enddate").strip()
	return datetime.datetime.strptime(line.split("=", 1)[1].replace(" GMT", ""), "%b %d %H:%M:%S %Y")


def needs_issue(cert):
	ends = expiry(cert)
	return ends is None or ends - datetime.datetime.utcnow() < datetime.timedelta(days=RENEW_WITHIN_DAYS)


def ensure(out):
	if not os.path.isfile(os.path.join(out, "ca.pem")) or not os.path.isfile(os.path.join(out, "ca.key")):
		make_ca(out)
		make_lobby(out, [])
		make_gameserver(out, "local", [])
		return
	if needs_issue(os.path.join(out, "ca.pem")):
		print("[make_certs] WARNING: the CA expires within " + str(RENEW_WITHIN_DAYS) + " days; run `make_certs.py ca` and reissue everything")
	if needs_issue(os.path.join(out, "lobby.pem")):
		make_lobby(out, [])
	folders = os.path.join(out, "gameservers")
	names = sorted(os.listdir(folders)) if os.path.isdir(folders) else []
	for name in names or ["local"]:
		if needs_issue(os.path.join(folders, name, "server.pem")) or needs_issue(os.path.join(folders, name, "client.pem")):
			make_gameserver(out, name, [])
	print("[make_certs] certificates ready in " + out)


def list_all(out):
	for folder, _, files in os.walk(out):
		for file in sorted(files):
			if file.endswith(".pem"):
				path = os.path.join(folder, file)
				print(os.path.relpath(path, out).ljust(36), "expires", expiry(path))


def main():
	global OPENSSL
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("what", nargs="?", choices=["ensure", "ca", "lobby", "gameserver"], default="ensure")
	parser.add_argument("name", nargs="?", help="the game server's name (gameserver only)")
	parser.add_argument("--host", action="append", default=[], help="an extra DNS name or IP the certificate is valid for")
	parser.add_argument("--out", default=DEFAULT_OUT)
	parser.add_argument("--list", action="store_true")
	args = parser.parse_args()
	OPENSSL = find_openssl()
	out = os.path.abspath(args.out)
	os.makedirs(out, exist_ok=True)
	if args.list:
		list_all(out)
	elif args.what == "ca":
		make_ca(out)
		print("[make_certs] every lobby and game server certificate must now be reissued, and clients given the new ca.pem")
	elif args.what == "lobby":
		make_lobby(out, args.host)
	elif args.what == "gameserver":
		make_gameserver(out, args.name, args.host)
	else:
		ensure(out)


if __name__ == "__main__":
	main()
