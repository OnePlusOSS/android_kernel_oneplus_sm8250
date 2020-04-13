/**
*	@file
*	@brief	計測ライブラリー							Ver 1.0.9.x
*/
/*============================================================================*/
#ifndef MEASUREMENT_LIBRARY_H_
#define MEASUREMENT_LIBRARY_H_


/*----------------------------------------------------------------------*/
/**
*	@brief	Mixing coefficient（mlCalMixCoef関数）用の入力値
*/
struct tagMlMixingValue
{
	double	radianX;
	double	radianY;

	double	hx45x;
	double	hy45x;
	double	hy45y;
	double	hx45y;

	UINT_8	hxsx;
	UINT_8	hysx;

	INT_32	hx45xL;		//! for Fixed point
	INT_32	hy45xL;		//! for Fixed point
	INT_32	hy45yL;		//! for Fixed point
	INT_32	hx45yL;		//! for Fixed point
};
/**
*	@brief	Mixing coefficient（mlCalMixCoef関数）用の入力値
*/
typedef	struct tagMlMixingValue		mlMixingValue;

/*----------------------------------------------------------------------*/
/**
*	@brief	Lineaity correction（mlCalLinearCorr関数）用の入力値
*/
struct tagMlLinearityValue
{
	INT_32	measurecount;	//! input parameter
	UINT_32	*dacX;			//! input parameter
	UINT_32	*dacY;			//! input parameter

	double	*positionX;
	double	*positionY;
	UINT_16	*thresholdX;
	UINT_16	*thresholdY;

	UINT_32	*coefAXL;		//! for Fixed point
	UINT_32	*coefBXL;		//! for Fixed point
	UINT_32	*coefAYL;		//! for Fixed point
	UINT_32	*coefBYL;		//! for Fixed point
};
/**
*	@brief	Linearity correction（mlCalLinearCorr関数）用の入力値
*/
typedef	struct tagMlLinearityValue		mlLinearityValue;

struct tagMlPoint
{
	double	X;
	double	Y;
};
/**
*	@brief	Linearity correction（mlCalLinearCorr関数）用の入力値
*/
typedef	struct tagMlPoint		mlPoint;


/*----------------------------------------------------------------------*/
/**
*	@brief	ライブラリーエラーコード
*/
enum tagErrorCode
{
	/**! エラー無しで正常終了 */
	ML_OK,

	/**! メモリ不足等メモリー関連のエラー */
	ML_MEMORY_ERROR,
	/**! 引数指定のエラー */
	ML_ARGUMENT_ERROR,
	/**! 引数にNULLが指令されているエラー */
	ML_ARGUMENT_NULL_ERROR,

	/**! 指定されたディレクトリが存在しないエラー */
	ML_DIRECTORY_NOT_EXIST_ERROR,
	/**! 画像ファイルが存在しないエラー */
	ML_FILE_NOT_EXIST_ERROR,
	/**! ファイルIOエラー */
	ML_FILE_IO_ERROR,
	/**! 未検出のマークが有り */
	ML_UNDETECTED_MARK_ERROR,
	/**! 同じ位置を示すマークが多重検出した */
	ML_MULTIPLEX_DETECTION_MARK_ERROR,
	/**! 必要なDLLが見つからないなど実行不可な状態 */
	ML_NOT_EXECUTABLE,

	/**! 未解析の画像が有りエラー */
	ML_THERE_UNANALYZED_IMAGE_ERROR,

	/**! 上記以外のエラー */
	ML_ERROR,
};

/**
*	@brief	ライブラリーエラーコード
*/
typedef	enum tagErrorCode	mlErrorCode;

#endif /* #ifndef MEASUREMENT_LIBRARY_H_ */
