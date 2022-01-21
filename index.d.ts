declare namespace activeWindow {

	interface Options {
		readonly screenRecordingPermission: boolean;
	}

	interface BaseOwner {
		name: string;
		processId: number;
		path: string;
	}

	interface BaseResult {
		title: string;
		owner: BaseOwner;

	}

	interface WindowsResult extends BaseResult {
		platform: 'windows';
		url?: string;
	}

	type Result = WindowsResult;
}

declare const activeWindow: {
	(options?: activeWindow.Options): Promise<activeWindow.Result | undefined>;

	sync(options?: activeWindow.Options): activeWindow.Result | undefined;
}

export = activeWindow;
