#!/usr/bin/env python3
#
# Render Qemu Block Graph
#
# Copyright (c) 2018 Virtuozzo International GmbH. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

import os
import sys
import subprocess
import json
from graphviz import Digraph

sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'python'))
from qemu.qmp import QMPError
from qemu.qmp.legacy import QEMUMonitorProtocol


def perm(arr):
    s = 'w' if 'write' in arr else '_'
    s += 'r' if 'consistent-read' in arr else '_'
    s += 'u' if 'write-unchanged' in arr else '_'
    s += 's' if 'resize' in arr else '_'
    return s


def render_block_graph(qmp, filename, format='png', bds_nodes=None, job_nodes=None, block_graph=None):
    '''
    Render graph in text (dot) representation into "@filename" and
    representation in @format into "@filename.@format"
    '''

    if bds_nodes is None:
        bds_nodes = qmp.command('query-named-block-nodes')
    bds_nodes = {n['node-name']: n for n in bds_nodes}

    if job_nodes is None:
        job_nodes = qmp.command('query-block-jobs')
    job_nodes = {n['device']: n for n in job_nodes}

    if block_graph is None:
        block_graph = qmp.command('x-debug-query-block-graph')

    graph = Digraph(comment='Block Nodes Graph')
    graph.format = format
    graph.node('permission symbols:\l'
               '  w - Write\l'
               '  r - consistent-Read\l'
               '  u - write - Unchanged\l'
               '  g - Graph-mod\l'
               '  s - reSize\l'
               'edge label scheme:\l'
               '  <child type>\l'
               '  <perm>\l'
               '  <shared_perm>\l', shape='none')

    for n in block_graph['nodes']:
        if n['type'] == 'block-driver':
            info = bds_nodes[n['name']]
            label = n['name'] + ' [' + info['drv'] + ']'
            if info['drv'] == 'file':
                label += '\n' + os.path.basename(info['file'])
            shape = 'ellipse'
        elif n['type'] == 'block-job':
            info = job_nodes[n['name']]
            label = info['type'] + ' job (' + n['name'] + ')'
            shape = 'box'
        else:
            assert n['type'] == 'block-backend'
            label = n['name'] if n['name'] else 'unnamed blk'
            shape = 'box'

        graph.node(str(n['id']), label, shape=shape)

    for e in block_graph['edges']:
        label = '%s\l%s\l%s\l' % (e['name'], perm(e['perm']),
                                  perm(e['shared-perm']))
        graph.edge(str(e['parent']), str(e['child']), label=label)

    graph.render(filename)


class LibvirtGuest():
    def __init__(self, name):
        self.name = name

    def command(self, cmd):
        # only supports qmp commands without parameters
        m = {'execute': cmd}
        ar = ['virsh', 'qemu-monitor-command', self.name, json.dumps(m)]

        reply = json.loads(subprocess.check_output(ar))

        if 'error' in reply:
            raise QMPError(reply)

        return reply['return']


if __name__ == '__main__':
    if len(sys.argv) == 3:
        obj = sys.argv[1]
        out = sys.argv[2]
        qnbn = None
        qbj = None
        xdqbg = None

        if os.path.exists(obj):
            # assume unix socket
            qmp = QEMUMonitorProtocol(obj)
            qmp.connect()
        else:
            # assume libvirt guest name
            qmp = LibvirtGuest(obj)
    else:
        qnbn = json.loads(sys.argv[1])
        qbj = json.loads(sys.argv[2])
        xdqbg = json.loads(sys.argv[3])
        out = sys.argv[4]
        qmp = None

    render_block_graph(qmp, out, 'png', qnbn, qbj, xdqbg)
