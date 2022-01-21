# devart-active-win

Getting information about the active window and process (title, description, path, processId, url)

Works on Windows.

## Install

```sh
npm i https://github.com/pityulin/devart-active-win.git --save-optional
```

## Usage

```js
const activeWindow = require('devart-active-win');

(async () => {
	console.log(await activeWindow(options));
	/*
	{
		title: "Google - Google Chrome",
		platform: "windows",
		owner: {
			processId: 9064,
			path: "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe",
			name: "Google Chrome",
		},
		url: "https://www.google.com/",
	}
})();
```

## API

### activeWindow(options?)

#### options

Type: `object`

##### screenRecordingPermission **(not used for compatibility)**

Type: `boolean`\

### activeWindow.sync(options?)

## Result

Returns a `Promise<object>` with the result, or `Promise<undefined>` if there is no active window or if the information is not available.

- `platform` *(string)* - `'windows'`
- `title` *(string)* - Window title
- `owner` *(Object)* - App that owns the window
	- `name` *(string)* - Name of the app or description
	- `processId` *(number)* - Process identifier
	- `path` *(string)* - Path to the app
- `url` *(string?)* - URL of the active browser tab if the active window is Google Chrome, Microsoft Edge, Firefox, Opera Internet Browser, Vivaldi

## OS support

It works only on Windows 7+.
