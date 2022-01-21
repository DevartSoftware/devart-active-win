'use strict';
const { windows } = require('../build/Release/grabber');

module.exports = () => Promise.resolve(windows());

module.exports.sync = windows;
