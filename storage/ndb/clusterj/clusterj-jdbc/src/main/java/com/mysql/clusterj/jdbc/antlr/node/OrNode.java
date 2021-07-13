/*
 *  Copyright (c) 2011, 2021, Oracle and/or its affiliates.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.0,
 *  as published by the Free Software Foundation.
 *
 *  This program is also distributed with certain software (including
 *  but not limited to OpenSSL) that is licensed under separate terms,
 *  as designated in a particular file or component or in included license
 *  documentation.  The authors of MySQL hereby grant you an additional
 *  permission to link the program and your derivative works with the
 *  separately licensed software that they have included with MySQL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License, version 2.0, for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

package com.mysql.clusterj.jdbc.antlr.node;

import org.antlr.runtime.Token;

import com.mysql.clusterj.query.Predicate;
import com.mysql.clusterj.query.QueryDomainType;

public class OrNode extends BooleanOperatorNode {

    public OrNode(Token token) {
        super(token);
    }

    public OrNode(OrNode orNode) {
        super(orNode);
    }

    @Override
    public OrNode dupNode() {
        return new OrNode(this);
    }

    @Override
    public Predicate getPredicate(QueryDomainType<?> queryDomainType) {
        Predicate result = null;
        PredicateNode leftNode = getLeftPredicateNode();
        PredicateNode rightNode = getRightPredicateNode();
        Predicate leftPredicate = leftNode.getPredicate(queryDomainType);
        Predicate rightPredicate = rightNode.getPredicate(queryDomainType);
        result = leftPredicate.or(rightPredicate);
        setNumberOfParameters(leftNode.getNumberOfParameters() + rightNode.getNumberOfParameters());
        return result;
    }

}
