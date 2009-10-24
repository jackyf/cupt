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

package Cupt::System::Worker::Lock;

use 5.10.0;
use strict;
use warnings;

use Fcntl qw(:flock);

use Cupt::Core;
use Cupt::LValueFields qw(_path _simulate _lock_fh);

sub new {
	my ($class, $path, $simulate) = @_;

	my $self = bless [] => $class;
	$self->_path = $path;
	$self->_simulate = $simulate;
	$self->_lock_fh = undef;

	return $self;
}

sub obtain {
	my ($self) = @_;

	if ($self->_simulate) {
		say sprintf __("simulating obtaining lock '%s'"), $self->_path;
	} else {
		open(my $fh, '>', $self->_path) or
				mydie("unable to open file '%s': %s", $self->_path, $!);
		flock($fh, LOCK_EX | LOCK_NB) or
				mydie("unable to obtain lock on file '%s': %s", $self->_path, $!);
		$self->_lock_fh = $fh;
	}
}

sub release {
	my ($self) = @_;

	if ($self->_simulate) {
		say sprintf __("simulating releasing lock '%s'"), $self->_path;
	} else {
		flock($self->_lock_fh, LOCK_UN) or
				mydie("unable to release lock on file '%s': %s", $self->_path, $!);
		close($self->_lock_fh) or
				mydie("unable to close file '%s': %s", $self->_path, $!);
	}
}

1;

