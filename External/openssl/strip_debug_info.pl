# Edits a configured OpenSSL Windows makefile to build without debug info:
# drops /Zi /Fd<pdb> from the compile flags and the install rule for ossl_static.pdb.
use strict;
use warnings;

my $file = shift or die "usage: strip_debug_info.pl makefile\n";
open my $in, '<:raw', $file or die "$file: $!\n";
my $text = do { local $/; <$in> };
close $in;

my $flags = ($text =~ s{^((?:LIB|DSO|BIN)_CFLAGS=.*?)/Zi /Fd\S+\s*}{$1}mg);
my $install = ($text =~ s{^\t\@if "\$\(SHLIBS\)"=="" \\\r?\n\t[^\n]*ossl_static\.pdb[^\n]*\n}{}m);
die "$file: no /Zi /Fd compile flags found\n" unless $flags;
die "$file: no ossl_static.pdb install rule found\n" unless $install;

open my $out, '>:raw', $file or die "$file: $!\n";
print $out $text;
close $out;
