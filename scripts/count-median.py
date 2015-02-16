#! /usr/bin/env python
#
# This file is part of khmer, http://github.com/ged-lab/khmer/, and is
# Copyright (C) Michigan State University, 2009-2015. It is licensed under
# the three-clause BSD license; see doc/LICENSE.txt.
# Contact: khmer-project@idyll.org
#
# pylint: disable=missing-docstring,invalid-name
"""
Count the median/avg k-mer abundance for each sequence in the input file,
based on the k-mer counts in the given k-mer counting table.  Can be used to
estimate expression levels (mRNAseq) or coverage (genomic/metagenomic).

% scripts/count-median.py <htname> <input seqs> <output counts>

Use '-h' for parameter help.

The output file contains sequence id, median, average, stddev, and seq length.

NOTE: All 'N's in the input sequences are converted to 'G's.
"""

from __future__ import print_function

import screed
import argparse
import khmer
import sys
from khmer.kfile import check_file_status, check_space
from khmer.khmer_args import info
import textwrap


def get_parser():
    epilog = """
    Count the median/avg k-mer abundance for each sequence in the input file,
    based on the k-mer counts in the given k-mer counting table.  Can be used
    to estimate expression levels (mRNAseq) or coverage (genomic/metagenomic).

    The output file contains sequence id, median, average, stddev, and seq
    length.

    NOTE: All 'N's in the input sequences are converted to 'G's.
    """
    parser = argparse.ArgumentParser(
        description='Count k-mers summary stats for sequences',
        epilog=textwrap.dedent(epilog))

    parser.add_argument('ctfile', metavar='input_counting_table_filename',
                        help='input k-mer count table filename')
    parser.add_argument('input', metavar='input_sequence_filename',
                        help='input FAST[AQ] sequence filename')
    parser.add_argument('output', metavar='output_summary_filename',
                        help='output summary filename')
    parser.add_argument('--version', action='version', version='%(prog)s '
                        + khmer.__version__)
    parser.add_argument('-f', '--force', default=False, action='store_true',
                        help='Overwrite output file if it exists')
    return parser


def main():
    info('count-median.py', ['diginorm'])
    args = get_parser().parse_args()

    htfile = args.ctfile
    input_filename = args.input
    output_filename = args.output

    infiles = [htfile, input_filename]
    for infile in infiles:
        check_file_status(infile, args.force)

    check_space(infiles, args.force)

    print('loading k-mer counting table from', htfile, file=sys.stderr)
    htable = khmer.load_counting_hash(htfile)
    ksize = htable.ksize()

    print('writing to', output_filename, file=sys.stderr)
    output = open(output_filename, 'w')

    for record in screed.open(input_filename):
        seq = record.sequence.upper()
        if 'N' in seq:
            seq = seq.replace('N', 'G')

        if ksize <= len(seq):
            medn, ave, stdev = htable.get_median_count(seq)
            print(record.name, medn, ave, stdev, len(seq), file=output)

if __name__ == '__main__':
    main()
