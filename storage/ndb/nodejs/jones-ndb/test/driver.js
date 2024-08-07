/*
 Copyright (c) 2013, 2024, Oracle and/or its affiliates.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License, version 2.0,
 as published by the Free Software Foundation.

 This program is designed to work with certain software (including
 but not limited to OpenSSL) that is licensed under separate terms,
 as designated in a particular file or component or in included license
 documentation.  The authors of MySQL hereby grant you an additional
 permission to link the program and your derivative works with the
 separately licensed software that they have either included with
 the program or referenced in the documentation.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License, version 2.0, for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

"use strict";

var jones = require("database-jones"), jonesNdb = require("jones-ndb"),
    jonesMysql = require("jones-mysql"), driver = require(jones.fs.test_driver),
    properties;

driver.processCommandLineOptions();
properties = driver.getConnectionProperties("ndb");

// driver.name is used in summary of results (see jones-test/lib/Result.js)
driver.name = "ndb";

// Setup globals:
global.test_conn_properties = properties;
global.mynode = jones;
global.adapter = "ndb";

/* Find and run all tests */
driver.addSuitesFromDirectory(jones.fs.suites_dir);
driver.addSuitesFromDirectory(jonesMysql.config.suites_dir);
driver.addSuitesFromDirectory(jonesNdb.config.suites_dir);
driver.runAllTests();
