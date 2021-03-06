# Locale::Po4a::Po -- manipulation of po files 
# $Id$
#
# This program is free software; you may redistribute it and/or modify it
# under the terms of GPL (see COPYING).

############################################################################
# Modules and declarations
############################################################################

=head1 NAME

Locale::Po4a::Po - po file manipulation module

=head1 SYNOPSIS    

    use Locale::Po4a::Po;
    my $pofile=Locale::Po4a::Po->new();

    # Read po file
    $pofile->read('file.po');

    # Add an entry
    $pofile->push('msgid' => 'Hello', 'msgstr' => 'bonjour', 
                  'flags' => "wrap", 'reference'=>'file.c:46');

    # Extract a translation
    $pofile->gettext("Hello"); # returns 'bonjour'

    # Write back to a file
    $pofile->write('otherfile.po');

=head1 DESCRIPTION

Locale::Po4a::Po is a module that allows you to manipulate message
catalogs. You can load and write from/to a file (which extension is often
I<po>), you can build new entries on the fly or request for the translation
of a string.

For a more complete description of message catalogs in the po format and
their use, please refer to the documentation of the gettext program.

This module is part of the PO4A project, which objective is to use po files
(designed at origin to ease the translation of program messages) to
translate everything, including documentation (man page, info manual),
package description, debconf templates, and everything which may benefit
from this.

=head1 OPTIONS ACCEPTED BY THIS MODULE

=over 4

=item porefs

This specifies the reference format. It can be one of 'none' to not produce
any reference, 'noline' to not specify the line number, and 'full' to
include complete references.

=back

=cut

use IO::File;


require Exporter;

package Locale::Po4a::Po;

use Locale::Po4a::Common qw(wrap_msg wrap_mod wrap_ref_mod dgettext);

use subs qw(makespace);
use vars qw(@ISA @EXPORT_OK);
@ISA = (Exporter);
@EXPORT_OK = qw(move_po_if_needed);

use 5.006;
use strict;
use warnings;

use Carp qw(croak);
use File::Path; # mkdir before write
use File::Copy; # move
use POSIX qw(strftime floor);
use Time::Local;

use Encode;

my @known_flags=qw(wrap no-wrap c-format fuzzy);

my %debug=('canonize'	=> 0,
           'quote'	=> 0,
           'escape'	=> 0,
           'encoding'   => 0,
           'filter'     => 0);

=head1 Functions about whole message catalogs

=over 4

=item new()

Creates a new message catalog. If an argument is provided, it's the name of
a po file we should load.

=cut

sub new {
    my ($this, $options) = (shift, shift);
    my $class = ref($this) || $this;
    my $self = {};
    bless $self, $class;
    $self->initialize($options);
 
    my $filename = shift;
    $self->read($filename) if defined($filename) && length($filename);
    return $self;
}

# Return the numerical timezone (e.g. +0200)
# Neither the %z nor the %s formats of strftime are portable:
# '%s' is not supported on Solaris and '%z' indicates
# "2006-10-25 19:36E. Europe Standard Time" on MS Windows.
sub timezone {
    my @g = gmtime();
    my @l = localtime();

    my $diff = floor(timelocal(@l)/60 +0.5) - floor(timelocal(@g)/60 + 0.5);

    my $h = floor($diff / 60) + $l[8]; # $l[8] indicates if we are currently
                                       # in a daylight saving time zone
    my $m = $diff%60;

    return sprintf "%+03d%02d\n", $h, $m;
}

sub initialize {
    my ($self, $options) = (shift, shift);
    my $date = strftime("%Y-%m-%d %H:%M", localtime).timezone();
    chomp $date;
#    $options = ref($options) || $options;

    $self->{options}{'porefs'}= 'full';
    foreach my $opt (keys %$options) {
	if ($options->{$opt}) {
	    die wrap_mod("po4a::po", dgettext ("po4a", "Unknown option: %s"), $opt) unless exists $self->{options}{$opt};
	    $self->{options}{$opt} = $options->{$opt};
	}
    }
    $self->{options}{'porefs'} =~ /^(full|noline|none)$/ ||
        die wrap_mod("po4a::po", 
                     dgettext ("po4a", "Invalid value for option 'porefs' ('%s' is not one of 'full', 'noline' or 'none')"), 
                     $self->{options}{'porefs'});

    $self->{po}=();
    $self->{count}=0;  # number of msgids in the PO
    # count_doc: number of strings in the document
    # (duplicate strings counted multiple times)
    $self->{count_doc}=0;
    $self->{header_comment}=
	escape_text( " SOME DESCRIPTIVE TITLE\n"
		    ." Copyright (C) YEAR Free Software Foundation, Inc.\n"
		    ." FIRST AUTHOR <EMAIL\@ADDRESS>, YEAR.\n"
		    ." \n"
		    .", fuzzy");
#    $self->header_tag="fuzzy";
    $self->{header}=escape_text("Project-Id-Version: PACKAGE VERSION\n".
				"POT-Creation-Date: $date\n".
				"PO-Revision-Date: YEAR-MO-DA HO:MI+ZONE\n".
				"Last-Translator: FULL NAME <EMAIL\@ADDRESS>\n".
				"Language-Team: LANGUAGE <LL\@li.org>\n".
				"MIME-Version: 1.0\n".
				"Content-Type: text/plain; charset=CHARSET\n".
				"Content-Transfer-Encoding: ENCODING");

    # To make stats about gettext hits
    $self->stats_clear();
}

=item read($)

Reads a po file (which name is given as argument).  Previously existing
entries in self are not removed, the new ones are added to the end of the
catalog.

=cut

sub read{
    my $self=shift;
    my $filename=shift 
	|| croak wrap_mod("po4a::po", dgettext("po4a", "Please provide a non-null filename"));

    my $fh;
    if ($filename eq '-') {
	$fh=*STDIN;
    } else {
	open $fh,"<$filename" 
	  || croak wrap_mod("po4a::po", dgettext("po4a", "Can't read from %s: %s"), $filename, $!);
    }

    ## Read paragraphs line-by-line
    my $pofile="";
    my $textline;
    while (defined ($textline = <$fh>)) {
	$pofile .= $textline;
    }
#    close INPUT || croak (sprintf(dgettext("po4a","Can't close %s after reading: %s"),$filename,$!)."\n");

    my $linenum=0;

    foreach my $msg (split (/\n\n/,$pofile)) {
        my ($msgid,$msgstr,$comment,$automatic,$reference,$flags,$buffer);
	foreach my $line (split (/\n/,$msg)) {
	    $linenum++;
	    if ($line =~ /^#\. ?(.*)/) {  # Automatic comment
		$automatic .= (defined($automatic) ? "\n" : "").$1;
		
	    } elsif ($line =~ /^#: ?(.*)/) { # reference
	        $reference .= (defined($reference) ? "\n" : "").$1;
		     
	    } elsif ($line =~ /^#, ?(.*)/) { # flags
		$flags .= (defined($flags) ? "\n" : "").$1;
		 
	    } elsif ($line =~ /^#(.*)/ || $line =~ /^#$/) {  # Translator comments 
	        $comment .= (defined($comment) ? "\n" : "").($1||"");

	    } elsif ($line =~ /^msgid (".*")/) { # begin of msgid
	        $buffer .= (defined($buffer) ? "\n" : "").$1;
		 
	    } elsif ($line =~ /^msgstr (".*")/) { # begin of msgstr, end of msgid
	        $msgid = $buffer;
	        $buffer = "$1";
	     
	    } elsif ($line =~ /^(".*")/) { # continuation of a line
	        $buffer .= "\n$1";
	    } else {
	        warn wrap_ref_mod("$filename:$linenum", "po4a::po", dgettext("po4a", "Strange line: -->%s<--"), $line);
	    }
	}
	$linenum++;
	$msgstr=$buffer;
	$msgid = unquote_text($msgid) if (defined($msgid));
	$msgstr = unquote_text($msgstr) if (defined($msgstr));
        $self->push_raw ('msgid'     => $msgid,
		        'msgstr'    => $msgstr,
		        'reference' => $reference,
		        'flags'     => $flags,
		        'comment'   => $comment,
		        'automatic' => $automatic);
    }
}

=item write($)

Writes the current catalog to the given file.

=cut

sub write{
    my $self=shift;
    my $filename=shift 
	or croak (dgettext("po4a","Can't write to a file without filename")."\n");

    my $fh;
    if ($filename eq '-') {
	$fh=\*STDOUT;
    } else {
	# make sure the directory in which we should write the localized file exists
	my $dir = $filename;
	if ($dir =~ m|/|) {
	    $dir =~ s|/[^/]*$||;
	
	    File::Path::mkpath($dir, 0, 0755) # Croaks on error
	      if (length ($dir) && ! -e $dir);
	}
	open $fh,">$filename" 
	    || croak wrap_mod("po4a::po", dgettext("po4a", "Can't write to %s: %s"), $filename, $!);
    }

    print $fh "".format_comment(unescape_text($self->{header_comment}),"") 
	if defined($self->{header_comment}) && length($self->{header_comment});

    print $fh "msgid \"\"\n";
    print $fh "msgstr ".quote_text($self->{header})."\n\n";


    my $first=1;
    foreach my $msgid ( sort { ($self->{po}{"$a"}{'pos'}) <=> 
 			       ($self->{po}{"$b"}{'pos'}) 
                             }  keys %{$self->{po}}) {
	my $output="";

	if ($first) {
	    $first=0;
	} else {
	    $output .= "\n";
	}
	
	$output .= format_comment($self->{po}{$msgid}{'comment'},"") 
	    if    defined($self->{po}{$msgid}{'comment'})
	       && length ($self->{po}{$msgid}{'comment'});
	if (   defined($self->{po}{$msgid}{'automatic'})
	    && length ($self->{po}{$msgid}{'automatic'})) {
	    foreach my $comment (split(/\\n/,$self->{po}{$msgid}{'automatic'}))
	    {
		$output .= format_comment($comment, ". ")
	    }
	}
	$output .= format_comment($self->{po}{$msgid}{'type'}," type: ") 
	    if    defined($self->{po}{$msgid}{'type'})
	       && length ($self->{po}{$msgid}{'type'});
	$output .= format_comment($self->{po}{$msgid}{'reference'},": ") 
	    if    defined($self->{po}{$msgid}{'reference'})
	       && length ($self->{po}{$msgid}{'reference'});
	$output .= "#, ". join(", ", sort split(/\s+/,$self->{po}{$msgid}{'flags'}))."\n"
	    if    defined($self->{po}{$msgid}{'flags'})
	       && length ($self->{po}{$msgid}{'flags'});

	if ($self->get_charset =~ /^utf-8$/i) {
	    my $msgstr = Encode::decode_utf8($self->{po}{$msgid}{'msgstr'});
	    $msgid = Encode::decode_utf8($msgid);
	    $output .= Encode::encode_utf8("msgid ".quote_text($msgid)."\n");
	    $output .= Encode::encode_utf8("msgstr ".quote_text($msgstr)."\n");
	} else {
	    $output .= "msgid ".quote_text($msgid)."\n";
	    $output .= "msgstr ".quote_text($self->{po}{$msgid}{'msgstr'})."\n";
	}
	print $fh $output;
    }   
#    print STDERR "$fh";
#    if ($filename ne '-') {
#	close $fh || croak (sprintf(dgettext("po4a","Can't close %s after writing: %s\n"),$filename,$!));
#    }
}

=item write_if_needed($$)

Like write, but if the PO or POT file already exists, the object will be
written in a temporary file which will be compared with the existing file
to check that the update is needed (this avoids to change a POT just to
update a line reference or the POT-Creation-Date field).

=cut

sub move_po_if_needed {
    my ($new_po, $old_po, $backup) = (shift, shift, shift);
    my $diff;

    if (-e $old_po) {
        $diff = qx(diff -q -I'^#:' -I'^"POT-Creation-Date:' -I'^"PO-Revision-Date:' $old_po $new_po);
        if ( $diff eq "" ) {
            unlink $new_po
                or die wrap_msg(dgettext("po4a","Can't unlink %s: %s."),
                                $new_po, $!);
            # touch the old PO
            my ($atime, $mtime) = (time,time);
            utime $atime, $mtime, $old_po;
        } else {
            if ($backup) {
                copy $old_po, $old_po."~"
                  or die wrap_msg(dgettext("po4a","Can't copy %s to %s: %s."),
                                  $old_po, $old_po."~", $!);
            } else {
            }
            move $new_po, $old_po
                or die wrap_msg(dgettext("po4a","Can't move %s to %s: %s."),
                                $new_po, $old_po, $!);
        }
    } else {
        move $new_po, $old_po
            or die wrap_msg(dgettext("po4a","Can't move %s to %s: %s."),
                            $new_po, $old_po, $!);
    }
}

sub write_if_needed {
    my $self=shift;
    my $filename=shift 
	or croak (dgettext("po4a","Can't write to a file without filename")."\n");

    if (-e $filename) {
        my ($tmp_filename);
        (undef,$tmp_filename)=File::Temp->tempfile($filename."XXXX",
                                                   DIR    => "/tmp",
                                                   OPEN   => 0,
                                                   UNLINK => 0);
        $self->write($tmp_filename);
        move_po_if_needed($tmp_filename, $filename);
    } else {
        $self->write($filename);
    }
}

=item gettextize($$)

This function produces one translated message catalog from two catalogs, an
original and a translation. This process is described in L<po4a(7)|po4a.7>,
section I<Gettextization: how does it work?>.

=cut

sub gettextize { 
    my $this = shift;
    my $class = ref($this) || $this;
    my ($poorig,$potrans)=(shift,shift);

    my $pores=Locale::Po4a::Po->new();
    
    my $please_fail = 0;
    my $toobad = dgettext("po4a",
  	"\nThe gettextization failed (once again). Don't give up, gettextizing is a subtle art, ".
	"but this is only needed once to convert a project to the gorgeous luxus offered ".
        "by po4a to translators.".
        "\nPlease refer to the po4a(7) documentation, the section \"HOWTO convert a pre-existing ".
        "translation to po4a?\" contains several hints to help you in your task");

    # Don't fail right now when the entry count does not match. Instead, give it a try so that the user
    # can see where we fail (which is probably where the problem is).
    if ($poorig->count_entries_doc() > $potrans->count_entries_doc()) {
	warn wrap_mod("po4a gettextize", dgettext("po4a",
	    "Original has more strings than the translation (%d>%d). ".
	    "Please fix it by editing the translated version to add some dummy entry."),
		$poorig->count_entries_doc() , $potrans->count_entries_doc());
        $please_fail = 1;      
    } elsif ($poorig->count_entries_doc() < $potrans->count_entries_doc()) {
	warn wrap_mod("po4a gettextize", dgettext("po4a",
	    "Original has less strings than the translation (%d<%d). ".
	    "Please fix it by removing the extra entry from the translated file. ".
	    "You may need an addendum (cf po4a(7)) to reput the chunk in place after gettextization. ".
	    "A possible cause is that a text duplicated in the original is not translated the same way each time. Remove one of the translations, and you're fine."),
               $poorig->count_entries_doc(), $potrans->count_entries_doc());
        $please_fail = 1;
    }

    if ( $poorig->get_charset =~ /^utf-8$/i ) {
	$potrans->to_utf8;
	$pores->set_charset("utf-8");
    } else {
	if ($potrans->get_charset eq "CHARSET") {
	    $pores->set_charset("ascii");
	} else {
	    $pores->set_charset($potrans->get_charset);
	}
    }
    print "Po character sets:\n".
	"  original=".$poorig->get_charset."\n".
	"  translated=".$potrans->get_charset."\n".
	"  result=".$pores->get_charset."\n"
	    if $debug{'encoding'};

    for (my ($o,$t)=(0,0) ;
	 $o<$poorig->count_entries_doc() && $t<$potrans->count_entries_doc();
	 $o++,$t++) {
	#
	# Extract some informations
	#
	my ($orig,$trans)=($poorig->msgid_doc($o),$potrans->msgid_doc($t));
#	print STDERR "Matches [[$orig]]<<$trans>>\n";

	my ($reforig,$reftrans)=($poorig->{po}{$orig}{'reference'},
				 $potrans->{po}{$trans}{'reference'});
	my ($typeorig,$typetrans)=($poorig->{po}{$orig}{'type'},
				   $potrans->{po}{$trans}{'type'});

	#
	# Make sure the type of both string exist
	#
	die wrap_mod("po4a gettextize", "Internal error: type of original string number %s isn't provided", $o)
	    if ($typeorig eq '');
	
	die wrap_mod("po4a gettextize", "Internal error: type of translated string number %s isn't provided", $o)
	    if ($typetrans eq '');

	#
	# Make sure both type are the same
	#
	if ($typeorig ne $typetrans){
	    $pores->write("/tmp/gettextization.failed.po");
	    die wrap_msg(dgettext("po4a",
	    	"po4a gettextization: Structure disparity between original and translated files:\n".
		"msgid (at %s) is of type '%s' while\n".
		"msgstr (at %s) is of type '%s'.\n".
		"Original text: %s\n".
		"Translated text: %s\n".
	        "(result so far dumped to /tmp/gettextization.failed.po)")."%s",
	        $reforig, $typeorig, $reftrans, $typetrans, $orig, $trans,$toobad);
	}

	# 
	# Push the entry
	#
	$pores->push_raw('msgid' => $orig, 'msgstr' => $trans, 
			 'flags' => ($poorig->{po}{$orig}{'flags'} ? $poorig->{po}{$orig}{'flags'} :"")." fuzzy",
	                 'type'  => $typeorig,
			 'reference' => $reforig,
			 'conflict' => 1)
	    unless (defined($pores->{po}{$orig})
	            and ($pores->{po}{$orig}{'msgstr'} eq $trans))
	# FIXME: maybe we should be smarter about what reference should be
	#        sent to push_raw.
    }
    die "$toobad\n" if $please_fail; # make sure we return a useful error message when entry count differ
    return $pores;
}

=item filter($)

This function extracts a catalog from an existing one. Only the entries having
a reference in the given file will be placed in the resulting catalog.

This function parses its argument, converts it to a perl function definition, 
eval this definition and filter the fields for which this function returns
true.

I love perl sometimes ;)

=cut

sub filter {
    my $self=shift;
    our $filter=shift;

    my $res;
    $res = Locale::Po4a::Po->new();

    # Parse the filter
    our $code="sub apply { return ";
    our $pos=0;
    our $length = length $filter;
    our @filter = split(//,$filter); # explode chars to parts. How to subscript a string in Perl?

    sub gloups {
	my $fmt=shift;
	my $space = "";
	for (1..$pos){
	    $space .= ' ';
	}
	die wrap_msg("$fmt\n$filter\n$space^ HERE");
    }
    sub showmethecode {
	return unless $debug{'filter'};
	my $fmt=shift;
	my $space="";
	for (1..$pos){
	    $space .= ' ';
	}
	print STDERR "$filter\n$space^ $fmt\n";#"$code\n";
    }
    
    # I dream of a lex in perl :-/
    sub parse_expression {
	showmethecode("Begin expression")
	    if $debug{'filter'};

	gloups("Begin of expression expected, got '%s'",$filter[$pos])
	  unless ($filter[$pos] eq '(');
	$pos ++; # pass the '('
	if ($filter[$pos] eq '&') {
	    # AND
	    $pos++;
	    showmethecode("Begin of AND")
		if $debug{'filter'};
	    $code .= "(";
	    while (1) {
		gloups ("Unfinished AND statement.") 
		  if ($pos == $length);
		parse_expression();
		if ($filter[$pos] eq '(') {
		    $code .= " && ";
		} elsif ($filter[$pos] eq ')') {
		    last; # do not eat that char
		} else {
		    gloups("End of AND or begin of sub-expression expected, got '%s'", $filter[$pos]);
		}
	    }
	    $code .= ")";
	} elsif ($filter[$pos] eq '|') {
	    # OR
	    $pos++;
	    $code .= "(";
	    while (1) {
		gloups("Unfinished OR statement.")
		  if ($pos == $length);
		parse_expression();
		if ($filter[$pos] eq '(') {
		    $code .= " || ";
		} elsif ($filter[$pos] eq ')') {
		    last; # do not eat that char
		} else {
		    gloups("End of OR or begin of sub-expression expected, got '%s'",$filter[$pos]);
		}
	    }
	    $code .= ")";
	} elsif ($filter[$pos] eq '!') {
	    # NOT
	    $pos++;
	    $code .= "(!";
	    gloups("Missing sub-expression in NOT statement.")
	      if ($pos == $length);
	    parse_expression();
	    $code .= ")";
	} else {
	    # must be an equal. Let's get field and argument
	    my ($field,$arg,$done);
	    $field = substr($filter,$pos);
	    gloups("EQ statement contains no '=' or invalid field name")
	      unless ($field =~ /([a-z]*)=/i); 
	    $field = lc($1);
	    $pos += (length $field) + 1;

	    # check that we've got a valid field name, and the number it referes to
	    my @names=qw(msgid msgstr reference flags comment automatic); # DO NOT CHANGE THE ORDER
	    my $fieldpos;
	    for ($fieldpos = 0; 
 		 $fieldpos < scalar @names && $field ne $names[$fieldpos];
		 $fieldpos++) {}
	    gloups("Invalid field name: %s",$field)
	      if $fieldpos == scalar @names; # not found
	    
	    # Now, get the argument value. It has to be between quotes, which can be escaped
	    # We point right on the first char of the argument (first quote already ate)
	    my $escaped = 0;
	    my $quoted = 0;
	    if ($filter[$pos] eq '"') {
		$pos++;
		$quoted = 1;
	    }
	    showmethecode(($quoted?"Quoted":"Unquoted")." argument of field '$field'")
		if $debug{'filter'};

	    while (!$done) {
		gloups("Unfinished EQ argument.")
		  if ($pos == $length);

		if ($quoted) {
		    if ($filter[$pos] eq '\\') {
			if ($escaped) {
			    $arg .= '\\';
			    $escaped = 0;
			} else {
			    $escaped = 1;
			}
		    } elsif ($escaped) {
			if ($filter[$pos] eq '"') {
			    $arg .= '"';
			    $escaped = 0;
			} else {
			    gloups("Invalid escape sequence in argument: '\\%s'",$filter[$pos]);
			}
		    } else {
			if ($filter[$pos] eq '"') {
			    $done = 1;
			} else {
			    $arg .= $filter[$pos];
			}
		    }
		} else {
		    if ($filter[$pos] eq ')') {
			$pos--; # counter the next ++ since we don't want to eat this char
			$done = 1;
		    } else {
			$arg .= $filter[$pos];
		    }
		}
		$pos++;
	    }
	    # and now, add the code to check this equality
	    $code .= "(\$_[$fieldpos] =~ m/$arg/)";
	    
	}
	showmethecode("End of expression")
	    if $debug{'filter'};
        gloups("Unfinished statement.")
	  if ($pos == $length);
	gloups("End of expression expected, got '%s'",$filter[$pos])
	  unless ($filter[$pos] eq ')');
	$pos++;
    }
    # And now, launch the beast, finish the function and use eval to construct this function.
    # Ok, the lack of lexer is a fair price for the eval ;)
    parse_expression();
    gloups("Garbage at the end of the expression")
      if ($pos != $length);
    $code .= "; }";
    print STDERR "CODE = $code\n"
	if $debug{'filter'};
    eval $code;
    die wrap_mod("po4a::po", dgettext("po4a", "Eval failure: %s"), $@)
      if $@;

    for (my $cpt=(0) ;
	 $cpt<$self->count_entries();
	 $cpt++) {

	my ($msgid,$ref,$msgstr,$flags,$type,$comment,$automatic);

	$msgid = $self->msgid($cpt);
	$ref=$self->{po}{$msgid}{'reference'};

	$msgstr= $self->{po}{$msgid}{'msgstr'};
	$flags =  $self->{po}{$msgid}{'flags'};
	$type = $self->{po}{$msgid}{'type'};
	$comment = $self->{po}{$msgid}{'comment'};
	$automatic = $self->{po}{$msgid}{'automatic'};

	$res->push_raw('msgid' => $msgid, 
		       'msgstr' => $msgstr,
		       'flags' => $flags,
	               'type'  => $type,
		       'reference' => $ref,
		       'comment' => $comment,
		       'automatic' => $automatic)
	       if (apply($msgid,$msgstr,$ref,$flags,$comment,$automatic)); # DO NOT CHANGE THE ORDER
    }
    # delete the apply subroutine
    # otherwise it will be redefined.
    undef &apply;
    return $res;
}

=item to_utf8()

Recodes to utf-8 the po's msgstrs. Does nothing if the charset is not
specified in the po file ("CHARSET" value), or if it's already utf-8 or
ascii.

=cut

sub to_utf8 { 
    my $this = shift;
    my $charset = $this->get_charset();

    unless ($charset eq "CHARSET" or
	    $charset =~ /^ascii$/i or
	    $charset =~ /^utf-8$/i) {
	foreach my $msgid ( keys %{$this->{po}} ) {
	    Encode::from_to($this->{po}{$msgid}{'msgstr'}, $charset, "utf-8");
	}
	$this->set_charset("utf-8");
    }
}

=back

=head1 Functions to use a message catalog for translations

=over 4

=item gettext($%)

Request the translation of the string given as argument in the current catalog.
The function returns the original (untranslated) string if the string was not
found.

After the string to translate, you can pass a hash of extra
arguments. Here are the valid entries:

=over

=item wrap

boolean indicating whether we can consider that whitespaces in string are
not important. If yes, the function canonizes the string before looking for
a translation, and wraps the result.

=item wrapcol

The column at which we should wrap (default: 76).

=back

=cut

sub gettext {
    my $self=shift;
    my $text=shift;
    my (%opt)=@_;
    my $res;
    my $translated = 0;

    return "" unless defined($text) && length($text); # Avoid returning the header.
    my $validoption="reference wrap wrapcol";
    my %validoption;

    map { $validoption{$_}=1 } (split(/ /,$validoption));
    foreach (keys %opt) {
	Carp::confess "internal error:  unknown arg $_.\n".
	              "Here are the valid options: $validoption.\n"
	    unless $validoption{$_};
    }
    
    $text=canonize($text)     
	if ($opt{'wrap'});

    my $esc_text=escape_text($text);

    $self->{gettextqueries}++;
    
    if ($self->{po}{$esc_text}  && 
	defined( $self->{po}{$esc_text}{'msgstr'} ) &&
	length( $self->{po}{$esc_text}{'msgstr'} ) && 
	!( ($self->{po}{$esc_text}{'flags'}||"") =~ /fuzzy/) ) { 

	$self->{gettexthits}++;
	$res= unescape_text($self->{po}{$esc_text}{'msgstr'});
	$translated = 1;
    } else {
	$res= $text;
	$translated = 0;
    }

    if ($opt{'wrap'}) {
        if ($self->get_charset =~ /^utf-8$/i) {
            $res=Encode::decode_utf8($res);
            $res=wrap ($res, $opt{'wrapcol'} || 76);
            $res=Encode::encode_utf8($res);
        } else {
            $res=wrap ($res, $opt{'wrapcol'} || 76);
        }
    }
#    print STDERR "Gettext >>>$text<<<(escaped=$esc_text)=[[[$res]]]\n\n";
    return ($translated,$res);
}

=item stats_get()

Returns statistics about the hit ratio of gettext since the last time that
stats_clear() was called. Please note that it's not the same
statistics than the one printed by msgfmt --statistic. Here, it's statistics
about recent usage of the po file, while msgfmt reports the status of the
file.  Example of use:

    [some use of the po file to translate stuff]

    ($percent,$hit,$queries) = $pofile->stats_get();
    print "So far, we found translations for $percent\%  ($hit of $queries) of strings.\n";

=cut

sub stats_get() {
    my $self=shift;
    my ($h,$q)=($self->{gettexthits},$self->{gettextqueries});
    my $p = ($q == 0 ? 100 : int($h/$q*10000)/100);

#    $p =~ s/\.00//;
#    $p =~ s/(\..)0/$1/;

    return ( $p,$h,$q );
}

=item stats_clear()

Clears the statistics about gettext hits.

=cut

sub stats_clear {
    my $self = shift;
    $self->{gettextqueries} = 0;
    $self->{gettexthits} = 0;
}

=back

=head1 Functions to build a message catalog

=over 4

=item push(%)

Push a new entry at the end of the current catalog. The arguments should
form a hash table. The valid keys are:

=over 4

=item msgid

the string in original language.

=item msgstr

the translation.

=item reference

an indication of where this string was found. Example: file.c:46 (meaning
in 'file.c' at line 46). It can be a space-separated list in case of
multiple occurrences.

=item comment

a comment added here manually (by the translators). The format here is free.

=item automatic

a comment which was automatically added by the string extraction
program. See the I<--add-comments> option of the B<xgettext> program for
more information.

=item flags

space-separated list of all defined flags for this entry. 

Valid flags are: c-text, python-text, lisp-text, elisp-text, librep-text,
smalltalk-text, java-text, awk-text, object-pascal-text, ycp-text,
tcl-text, wrap, no-wrap and fuzzy. 

See the gettext documentation for their meaning.

=item type

This is mostly an internal argument: it is used while gettextizing
documents. The idea here is to parse both the original and the translation
into a po object, and merge them, using one's msgid as msgid and the
other's msgid as msgstr. To make sure that things get ok, each msgid in po
objects are given a type, based on their structure (like "chapt", "sect1",
"p" and so on in docbook). If the types of strings are not the same, that
means that both files do not share the same structure, and the process
reports an error.

This information is written as automatic comment in the po file since this
gives to translators some context about the strings to translate.

=item wrap

boolean indicating whether whitespaces can be mangled in cosmetic
reformattings. If true, the string is canonized before use.

This information is written to the po file using the 'wrap' or 'no-wrap' flag.

=item wrapcol

The column at which we should wrap (default: 76).

This information is not written to the po file.

=back

=cut

sub push {
    my $self=shift;
    my %entry=@_;

    my $validoption="wrap wrapcol type msgid msgstr automatic flags reference";
    my %validoption;

    map { $validoption{$_}=1 } (split(/ /,$validoption));
    foreach (keys %entry) {
	Carp::confess "internal error:  unknown arg $_.\n".
	              "Here are the valid options: $validoption.\n"
	    unless $validoption{$_};
    }

    unless ($entry{'wrap'}) {
	$entry{'flags'} .= " no-wrap";
    }
    if (defined ($entry{'msgid'})) {
	$entry{'msgid'} = canonize($entry{'msgid'})
	    if ($entry{'wrap'});

	$entry{'msgid'} = escape_text($entry{'msgid'});
    }
    if (defined ($entry{'msgstr'})) {
	$entry{'msgstr'} = canonize($entry{'msgstr'})
	    if ($entry{'wrap'});

	$entry{'msgstr'} = escape_text($entry{'msgstr'});
    }

    $self->push_raw(%entry);
}

# The same as push(), but assuming that msgid and msgstr are already escaped
sub push_raw {
    my $self=shift;
    my %entry=@_;
    my ($msgid,$msgstr,$reference,$comment,$automatic,$flags,$type)=
	($entry{'msgid'},$entry{'msgstr'},
	 $entry{'reference'},$entry{'comment'},$entry{'automatic'},
	 $entry{'flags'},$entry{'type'});
    my $keep_conflict = $entry{'conflict'};

#    print STDERR "Push_raw\n";
#    print STDERR " msgid=>>>$msgid<<<\n" if $msgid;
#    print STDERR " msgstr=[[[$msgstr]]]\n" if $msgstr;
#    Carp::cluck " flags=$flags\n" if $flags;
    
    return unless defined($entry{'msgid'});

    #no msgid => header definition
    unless (length($entry{'msgid'})) { 
#	if (defined($self->{header}) && $self->{header} =~ /\S/) {
#	    warn dgettext("po4a","Redefinition of the header. The old one will be discarded\n");
#	} FIXME: do that iff the header isn't the default one.
	$self->{header}=$msgstr;
	$self->{header_comment}=$comment;
	return;
    }

    if ($self->{options}{'porefs'} eq "none") {
        $reference = "";
    } elsif ($self->{options}{'porefs'} eq "noline") {
        $reference =~ s/:[0-9]*/:1/g;
    }

    if (defined($self->{po}{$msgid})) {
        warn wrap_mod("po4a::po", dgettext("po4a","msgid defined twice: %s"), $msgid) if (0); # FIXME: put a verbose stuff
	if ($msgstr && $self->{po}{$msgid}{'msgstr'} 
	    && $self->{po}{$msgid}{'msgstr'} ne "$msgstr") {
	    my $txt=quote_text($msgid);
	    my ($first,$second)=
		(format_comment(". ",$self->{po}{$msgid}{'reference'}).
		 quote_text($self->{po}{$msgid}{'msgstr'}),
		    
		 format_comment(". ",$reference).
		 quote_text($msgstr));

	    if ($keep_conflict) {
		$msgstr = "#-#-#-#-#  choice  #-#-#-#-#\\n".
		          $self->{po}{$msgid}{'msgstr'}."\\n".
		          "#-#-#-#-#  choice  #-#-#-#-#\\n".
		          "$msgstr";
		$msgstr = "#-#-#-#-#  choice  #-#-#-#-#\\n".$msgstr
		    unless ($msgstr =~ m/^#-#-#-#-#  choice  #-#-#-#-#\\n/s);
	    } else {
	    warn wrap_msg(dgettext("po4a",
	    	"Translations don't match for:\n".
		"%s\n".
		"-->First translation:\n".
		"%s\n".
		" Second translation:\n".
		"%s\n".
		" Old translation discarded."),$txt,$first,$second);
	    }
	}
    }
    $self->{po}{$msgid}{'reference'} = (defined($self->{po}{$msgid}{'reference'}) ? 
                                          $self->{po}{$msgid}{'reference'}." " : "")
                                         . $reference
       if (defined($reference));
    $self->{po}{$msgid}{'msgstr'} = $msgstr;
    $self->{po}{$msgid}{'comment'} = $comment;
    $self->{po}{$msgid}{'automatic'} = $automatic;
    if (defined($self->{po}{$msgid}{'pos_doc'})) {
        $self->{po}{$msgid}{'pos_doc'} .= " ".$self->{count_doc}++;
    } else {
        $self->{po}{$msgid}{'pos_doc'}  = $self->{count_doc}++;
    }
    unless (defined($self->{po}{$msgid}{'pos'})) {
      $self->{po}{$msgid}{'pos'} = $self->{count}++;
    }
    $self->{po}{$msgid}{'type'} = $type;
      
    if (defined($flags)) {
        $flags = " $flags ";
        $flags =~ s/,/ /g;
	foreach my $flag (@known_flags) {
	    if ($flags =~ /\s$flag\s/) { # if flag to be set
		unless (defined($self->{po}{$msgid}{'flags'}) && $self->{po}{$msgid}{'flags'} =~ /\b$flag\b/) {
		    # flag not already set
		    $self->{po}{$msgid}{'flags'} .= (defined($self->{po}{$msgid}{'flags'}) ? " " : "") .$flag;
		}
            }
        }
    }
#    print STDERR "stored ((($msgid)))=>(((".$self->{po}{$msgid}{'msgstr'}.")))\n\n";

}

=back

=head1 Miscellaneous functions

=over 4

=item count_entries()

Returns the number of entries in the catalog (without the header).

=cut

sub count_entries($) {
    my $self=shift;
    return $self->{count};
}

=item count_entries_doc()

Returns the number of entries in document. If a string appears multiple times
in the document, it will be counted multiple times

=cut

sub count_entries_doc($) {
    my $self=shift;
    return $self->{count_doc};
}

=item msgid($)

Returns the msgid of the given number.

=cut

sub msgid($$) {
    my $self=shift;
    my $num=shift;
    
    foreach my $msgid ( keys %{$self->{po}} ) {
	return $msgid if ($self->{po}{$msgid}{'pos'} eq $num);
    }
    return undef;
}

=item msgid_doc($)

Returns the msgid with the given position in the document.

=cut

sub msgid_doc($$) {
    my $self=shift;
    my $num=shift;
    
    foreach my $msgid ( keys %{$self->{po}} ) {
        foreach my $pos (split / /, $self->{po}{$msgid}{'pos_doc'}) {
            return $msgid if ($pos eq $num);
        }
    }
    return undef;
}

=item get_charset()

Returns the character set specified in the po header. If it hasn't been
set, it will return "CHARSET".

=cut

sub get_charset() {
    my $self=shift;
    $self->{header} =~ /charset=(.*?)[\s\\]/;
    return $1;
}

=item set_charset($)

This sets the character set of the po header to the value specified in its
first argument. If you never call this function (and no file with a specified
character set is read), the default value is left to "CHARSET". This value
doesn't change the behavior of this module, it's just used to fill that field
in the header, and to return it in get_charset().

=cut

sub set_charset() {
    my $self=shift;

    my ($newchar,$oldchar);
    $newchar = shift;
    $oldchar = $self->get_charset();

    $self->{header} =~ s/$oldchar/$newchar/;
}

#----[ helper functions ]---------------------------------------------------

# transforme the string from its po file representation to the form which 
#   should be used to print it
sub unescape_text {
    my $text = shift;

    print STDERR "\nunescape [$text]====" if $debug{'escape'};
    $text = join("",split(/\n/,$text));
    $text =~ s/\\"/"/g;
    # unescape newlines
    #   NOTE on \G:
    #   The following regular expression introduce newlines.
    #   Thus, ^ doesn't match all beginnings of lines.
    #   \G is a zero-width assertion that matches the position
    #   of the previous substitution with s///g. As every 
    #   substitution ends by a newline, it always matches a
    #   position just after a newline.
    $text =~ s/(           # $1:
                (\G|[^\\]) #    beginning of the line or any char
                           #    different from '\'
                (\\\\)*    #    followed by any even number of '\'
               )\\n        # and followed by an escaped newline
              /$1\n/sgx;   # single string, match globally, allow comments
    # unescape tabulations
    $text =~ s/(          # $1:
                (\G|[^\\])#    beginning of the line or any char
                          #    different from '\'
                (\\\\)*   #    followed by any even number of '\'
               )\\t       # and followed by an escaped tabulation
              /$1\t/mgx;  # multilines string, match globally, allow comments
    # and unescape the escape character
    $text =~ s/\\\\/\\/g;
    print STDERR ">$text<\n" if $debug{'escape'};

    return $text;
}

# transform the string to its representation as it should be written in po files
sub escape_text {
    my $text = shift;
    
    print STDERR "\nescape [$text]====" if $debug{'escape'};
    $text =~ s/\\/\\\\/g;
    $text =~ s/"/\\"/g;
    $text =~ s/\n/\\n/g;
    $text =~ s/\t/\\t/g;
    print STDERR ">$text<\n" if $debug{'escape'};
    
    return $text;
}

# put quotes around the string on each lines (without escaping it)
# It does also normalize the text (ie, make sure its representation is wraped 
#   on the 80th char, but without changing the meaning of the string) 
sub quote_text {
  my $string = shift;

  return '""' unless defined($string) && length($string);

  print STDERR "\nquote [$string]====" if $debug{'quote'};
  # break lines on newlines, if any
  # see unescape_text for an explanation on \G
  $string =~ s/(           # $1:
                (\G|[^\\]) #    beginning of the line or any char
                           #    different from '\'
                (\\\\)*    #    followed by any even number of '\'
               \\n)        # and followed by an escaped newline
              /$1\n/sgx;   # single string, match globally, allow comments
  $string = wrap($string);
  my @string = split(/\n/,$string);
  $string = join ("\"\n\"",@string);
  $string = "\"$string\"";
  if (scalar @string > 1 && $string[0] ne '') {
      $string = "\"\"\n".$string;
  }

  print STDERR ">$string<\n" if $debug{'quote'};
  return $string;
}

# undo the work of the quote_text function
sub unquote_text {
  my $string = shift;
  print STDERR "\nunquote [$string]====" if $debug{'quote'};
  $string =~ s/^""\\n//s;
  $string =~ s/^"(.*)"$/$1/s;
  $string =~ s/"\n"//gm;
  # Note: an even number of '\' could precede \\n, but I could not build a
  # document to test this
  $string =~ s/([^\\])\\n\n/$1!!DUMMYPOPM!!/gm;
  $string =~ s|!!DUMMYPOPM!!|\\n|gm;
  print STDERR ">$string<\n" if $debug{'quote'};
  return $string;
}

# canonize the string: write it on only one line, changing consecutive whitespace to
# only one space.
# Warning, it changes the string and should only be called if the string is plain text
sub canonize {
    my $text=shift;
    print STDERR "\ncanonize [$text]====" if $debug{'canonize'};
    $text =~ s/^ *//s;
    $text =~ s/^[ \t]+/  /gm;
    # if ($text eq "\n"), it messed up the first string (header)
    $text =~ s/\n/  /gm if ($text ne "\n");
    $text =~ s/([.)])  +/$1  /gm;
    $text =~ s/([^.)])  */$1 /gm;
    $text =~ s/ *$//s;
    print STDERR ">$text<\n" if $debug{'canonize'};
    return $text;
}

# wraps the string. We don't use Text::Wrap since it mangles whitespace at
# the end of splited line
sub wrap {
    my $text=shift;
    return "0" if ($text eq '0');
    my $col=shift || 76;
    my @lines=split(/\n/,"$text");
    my $res="";
    my $first=1;
    while (defined(my $line=shift @lines)) {
	if ($first && length($line) > $col - 10) {
	    unshift @lines,$line;
	    $first=0;
	    next;
	}
	if (length($line) > $col) {
	    my $pos=rindex($line," ",$col);
	    while (substr($line,$pos-1,1) eq '.' && $pos != -1) {
		$pos=rindex($line," ",$pos-1);
	    }
	    if ($pos == -1) {
		# There are no spaces in the first $col chars, pick-up the
		# first space
		$pos = index($line," ");
	    }
	    if ($pos != -1) {
		my $end=substr($line,$pos+1);
		$line=substr($line,0,$pos+1);
		if ($end =~ s/^( +)//) {
		    $line .= $1;
		}
		unshift @lines,$end;
	    }
	}
	$first=0;
	$res.="$line\n";
    }
    # Restore the original trailing spaces
    $res =~ s/\s+$//s;
    if ($text =~ m/(\s+)$/s) {
	$res .= $1;
    }
    return $res;
}

# outputs properly a '# ... ' line to be put in the po file
sub format_comment {
  my $comment=shift;
  my $char=shift;
  my $result = "#". $char . $comment;
  $result =~ s/\n/\n#$char/gs;
  $result =~ s/^#$char$/#/gm; 
  $result .= "\n";
  return $result;
}


1;
__END__

=back

=head1 AUTHORS

 Denis Barbier <barbier@linuxfr.org>
 Martin Quinson (mquinson#debian.org)

=cut
