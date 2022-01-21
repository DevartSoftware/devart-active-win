'use strict';
const { windows } = require('../build/Release/grabber');

module.exports = async options => {
	Promise.resolve(windows());
}

module.exports.sync = options => {
	windows;
}
