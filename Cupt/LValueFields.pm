#***************************************************************************
#*   Copyright (C) 2009 by Eugene V. Lyubimkin                             *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU General Public License                  *
#*   (version 3 or above) as published by the Free Software Foundation.    *
#*                                                                         *
#*   This program is distributed in the hope that it will be useful,       *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU General Public License for more details.                          *
#*                                                                         *
#*   You should have received a copy of the GNU GPL                        *
#*   along with this program; if not, write to the                         *
#*   Free Software Foundation, Inc.,                                       *
#*   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA               *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the Artistic License, which comes with Perl     *
#***************************************************************************
package Cupt::LValueFields;

use 5.10.0;
use strict;
use warnings;

=begin comment

Own helper to build array-based lvalue-fielded classes

=cut

sub import {
	my ($class, @field_names) = @_;
	my $caller = caller();

	my $field_offset = 0;
	if ($field_names[0] =~ m/\d+/) {
		$field_offset = shift @field_names;
	}

	foreach my $field_name (@field_names) {
		## no critic (StringyEval)
		my $offset_sub_name = "${field_name}_offset";
		my $eval_string = "package $caller;" .
			"sub $field_name (\$) : lvalue { \$_[0]->[$field_offset] } " .
			"use constant $offset_sub_name => $field_offset;";
		eval $eval_string;
		++$field_offset;
	}
	return;
}

1;

