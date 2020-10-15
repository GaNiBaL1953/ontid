<?php

use Aeliot\Db\DbHelper;
use Aeliot\Utilities\Breadcrumbs;
use modules\customer\visitor\models\Visitor;
use modules\products\catalog\helpers\CatalogImage;
use modules\products\inventory\models\Inventory;
use modules\site_search\models\SiteSearchPreprocess;
use Pal9\Utilities\Mailer;

//* прод
const URL_SEND_DOCUMENT_BY_TASK = '';
const URL_SEND_DOCUMENT_BY_CAMERA = '';
const URL_GET_CATEGORY_INFO = '';//информация о категории
const URL_SEND_INFO_PRODUCT_DEFECT = '';  // для брака
const URL_GET_AMOUNT_PRODUCT = '';
const URL_GET_INFO_PRODUCT_BY_BARCODE = '';
const URL_GET_INFO_PRODUCT_BY_OKDP = '';
const URL_CHANGE_CATEGORY = '';
const URL_CHECKING_TASK_COMPLETION = '';

const NUMBER_ROLE_MASTAK = 10;

/** Базовая страница Инвентаризации
 * @return mixed|string|string[]|void|null
 */
function inventoryPage()
{
    $data['count'] = getTasksNotInWork((int)Visitor::getWarehouseId());

    $breadcrumbs = new Breadcrumbs();
    $breadcrumbs->populate();
    $breadcrumbs->add('Инвентаризация', 'inventory');

    return theme('inventory_page', $data, $breadcrumbs);
}

/** Страница настроек пользователя инвентаризации
 * @return mixed|string|string[]|void|null
 */
function inventoryPageSettings()
{
    $data['warehouses'] = Inventory::getWarehousesInventoryMastak();

    if (isset($_POST['mail_warehouse'])) {
        foreach ($data['warehouses'] as $warehouseId => $warehouseName) {
            variable_set('mail_warehouseId_' . $warehouseId, $_POST['mail_warehouseId_' . $warehouseId]);
        }
    }

    if (isset($_POST["report_by_error"])) {
        $dateFrom = isset($_POST["dateFrom"]) ? $_POST["dateFrom"] : date("Y-m-01", strtotime("-1 month"));
        $dateBy = isset($_POST["dateBy"]) ? $_POST["dateBy"] : date("Y-m-01");

        $query = DbHelper::getQuery();
        $query->select(
            [
                $query->qn('message'),
                $query->qn('timestamp'),
            ]
        )
            ->from($query->qn('watchdog'))
            ->where(
                $query->qn('timestamp') . ' > ' . strtotime($dateFrom)
                . ' AND ' . $query->qn('timestamp') . ' <' . strtotime($dateBy)
            );

        $query->where($query->qn('type') . ' LIKE "%inventory%"');
        $res = db_query($query);

        while ($item = db_fetch_array($res)) {
            $item['timestamp'] = date("Y-m-d H:i:s", $item['timestamp']);
            $data['messages'][] = $item;
        }
    }

    if (!empty($_POST["search"]) || (!empty($_POST['selectWarehouses']) && is_numeric($_POST['selectWarehouses']))) {
        $query = DbHelper::getQuery();
        $query->select(
            [
                $query->qn('u.uid'),
                $query->qn('u.name', 'name'),
                $query->qn('u.mail', 'mail'),
                $query->qn('c.phone', 'phone'),
                $query->qn('c.warehouse_id', 'warehouseId'),
                $query->qn('ur.rid', 'rid'),
            ]
        )
            ->from($query->qn('users', 'u'))
            ->leftJoin(
                $query->qn('customers', 'c')
                . ' ON ' . $query->qn('c.uid') . ' = ' . $query->qn('u.uid')
            )
            ->leftJoin(
                $query->qn('users_roles', 'ur')
                . ' ON ' . $query->qn('ur.uid') . ' = ' . $query->qn('u.uid')
                . ' AND ' . $query->qn('ur.rid') . ' = ' . NUMBER_ROLE_MASTAK
            )
            ->order($query->qn('name') . ' DESC');
        if (!empty($_POST["search"])) {
            $searchLogin = '%' . trim((string)$_POST["search"]) . '%';
            $query->where(
                [
                    $query->qn('name') . ' LIKE "' . $searchLogin . '" OR '
                    . $query->qn('mail') . ' LIKE "' . $searchLogin . '" OR'
                    . $query->qn('phone') . ' LIKE "' . $searchLogin . '" '
                ]
            );
        }
        if (!empty($_POST["selectWarehouses"]) && is_numeric($_POST['selectWarehouses'])) {
            $data['selectWarehouses'] = (int)$_POST["selectWarehouses"];
            $query->where($query->qn('warehouse_id') . ' = ' . (int)$_POST["selectWarehouses"]);
        }
        $res = db_query((string)$query);
        while ($item = db_fetch_object($res)) {
            if (!empty($item->warehouseId)) {
                $item->warehouseCategories = Inventory::getCategoryInventory($item->warehouseId);
                $item->categoryUid = Visitor::getUserCategoryInventory((int)$item->uid);
            }

            $item->rid = !empty($item->rid) ? 'checked' : '';
            $data['user'][] = $item;
        }
        if (empty($data['user'])) {
            $data['errors'] = " Подходящих результатов нет ";
        }
    }
    return theme('inventory_page_settings', $data);
}

/**
 * Получение html блока выбора категорий для привязки пользователя
 */
function getCategoryInWarehouse()
{
    $result = [];

    if (!empty($_POST['warehouseIdUser'] && is_numeric($_POST['warehouseIdUser']))) {
        $categories = Inventory::getCategoryInventory((int)$_POST['warehouseIdUser']);

        if (isset($categories) && !empty($categories)) {
            $result['categories'] = '<select class="selectCategory" name="selectCategory" multiple>';
            $userCategory = [];
            if (!empty($_POST['uid'])) {
                $userCategory = Visitor::getUserCategoryInventory((int)$_POST['uid']);
            }

            foreach ($categories as $categoryUid => $categoryName) {
                $checked = in_array($categoryUid, $userCategory) ? 'selected' : '';
                $result['categories'] .= '<option value="' . $categoryUid . '" ' . $checked . '>' . $categoryName . '</option>';
            }
            $result['categories'] .= '</select>';
        }
    }
    $result['categories'] = !empty($result['categories']) ? $result['categories'] : 'Категории не обнаружены';
    exit(json_encode($result));
}

/**
 * Сохранение настроек пользователя инвентаризации Мастака
 */
function saveSettingUser()
{
    $warehouseIdUser = is_numeric($_POST['warehouseIdUser']) ? (int)$_POST['warehouseIdUser'] : 0;
    $uid = (int)$_POST['uid'];
    try {
        db_query('START TRANSACTION');
        db_query('UPDATE `{customers}` SET `warehouse_id` = %d WHERE `uid` = %d', $warehouseIdUser, $uid);

        Inventory::deleteUserCategoryInventory($uid);

        if ($_POST['selectCategory'] != 'undefined' && $_POST['selectCategory'] != 'null') {
            $selectCategory = explode(',', $_POST['selectCategory']);
            Inventory::insertUserCategoryInventory($selectCategory, $uid);
        }

        if ($_POST['roleMastak'] === 'true') {
            db_query(
                "INSERT INTO `users_roles` (`uid`, `rid`)
                VALUES (%d, %d)
                ON DUPLICATE KEY UPDATE 
                `uid` = VALUES(`uid`),
                `rid` = VALUES(`rid`)",
                $uid,
                NUMBER_ROLE_MASTAK
            );
        } else {
            db_query('DELETE FROM `{users_roles}` WHERE `uid`=%d AND `rid`=%d', $uid, NUMBER_ROLE_MASTAK);
        }

        db_query('COMMIT');
        $result = '<p class="result_success">Данные сохранены успешно</p>';
    } catch (RuntimeException $e) {
        db_query('ROLLBACK');
        $result = '<p class="result_error">Ошибка при сохранении данных</p>';
    }
    exit(json_encode($result));
}

/**
 * Получение от 1С списка задач
 */
function receptionInventory()
{
    if (checkAuthorization()) {
        $query = DbHelper::getQuery();
        $result = json_decode(file_get_contents("php://input"));

        $tasksInWork = getTasksInWork();
        $categoryInfo = Inventory::getCategoryInventory();
        $tasksIns = [];
        foreach ($result as $task) {
            if (!in_array($task->{"УИД"}, $tasksInWork) && $task->{"КодСклада7745"} != 0) {
                $dataStart = strtotime($task->{"Дата"});

                if (!isset($categoryInfo[$task->{"КатегорияУИД"}])) {
                    $titleCategory = getCategoryFrom1C($task->{"КатегорияУИД"}, $task->{"КодСклада7745"});
                    if (!empty($titleCategory)) {
                        Inventory::insertCategoryInfo(
                            $task->{"КатегорияУИД"},
                            $titleCategory,
                            (int)$task->{"КодСклада7745"}
                        );
                    }
                    Inventory::addNewCategoryUsers($task->{"КатегорияУИД"}, $task->{"КодСклада7745"});
                }

                $values = '('
                    . $query->q($task->{"УИД"}) . ','
                    . $query->q($task->{"Номер"}) . ','
                    . $dataStart . ','
                    . ($dataStart + getSecondsBeforeDataFinish($task->{"ВремяЖизни"})) . ','
                    . $query->q($task->{"КатегорияУИД"}) . ','
                    . $query->q($task->{"КодСклада7745"}) . ','
                    . $query->q($task->{"КодВоВнешнейИБ"}) . ','
                    . $query->q($task->{"ШтрихКод"}) . ','
                    . $query->q($task->{"Количество"}) . ','
                    . $query->q($task->{"Себестоимость"}) . ','
                    . Inventory::TASK_DONT_IN_WORK
                    . ')';

                $tasksIns[] = $values;
            }
        }

        $inFieldsQn = $query->qn(
            [
                'document_uid',
                'number',
                'timestamp',
                'lifetime',
                'category_uid',
                'codeWarehouse',
                'OKDP',
                'barcode',
                'quantity',
                'cost_price',
                'status'
            ]
        );

        $query->insert($query->qn('inventory_tasks_mastak'))
            ->fields($inFieldsQn)
            ->values($tasksIns)
            ->onDuplicateFields($inFieldsQn);

        db_query($query);
    }
}

/** Вычисление времени жизни задачи
 * @param string $data
 * @return float|int|mixed|string
 */
function getSecondsBeforeDataFinish(string $data)
{
    $arr = explode(':', $data);
    return $arr[0] * 3600 + $arr[1] * 60 + $arr[2];
}

/**
 * Получение от 1С информации о категориях в инвентаризации
 */
function receptionInventoryCategory()
{
    if (checkAuthorization()) {
        $categoryInfo = json_decode(file_get_contents("php://input"));

        $categoryOld = Inventory::getCategoryInventory();
        $categoryInfo->{"КодСклада7745"} = is_numeric(
            $categoryInfo->{"КодСклада7745"}
        ) ? (int)$categoryInfo->{"КодСклада7745"} : 0;

        if (isset($categoryOld[$categoryInfo->{"УИД"}]) && $categoryInfo->{"ПометкаУдаления"}) {
            Inventory::deleteCategory($categoryInfo->{"УИД"});
        } else {
            Inventory::insertCategoryInfo(
                $categoryInfo->{"УИД"},
                $categoryInfo->{"Наименование"},
                (int)$categoryInfo->{"КодСклада7745"}
            );

            if (!isset($categoryOld[$categoryInfo->{"УИД"}])) {
                Inventory::addNewCategoryUsers($categoryInfo->{"УИД"}, (int)$categoryInfo->{"КодСклада7745"});
            }
        }
    }
}

/**
 *  Получение списка задач в работе
 * @return array
 */
function getTasksInWork(): array
{
    $tasks = [];
    $query = DbHelper::getQuery();

    $query->select($query->qn('document_uid'))
        ->from($query->qn('inventory_tasks_mastak'))
        ->where($query->qn('status') . '!= ' . Inventory::TASK_DONT_IN_WORK);
    $result = db_query($query);

    while ($row = db_fetch_object($result)) {
        $tasks[] = $row->document_uid;
    }
    return $tasks;
}

/**
 *  Получение количества задач не в работе
 * @param int $warehouseId
 * @return bool|mixed
 */
function getTasksNotInWork(int $warehouseId = 0)
{
    $query = DbHelper::getQuery();
    $query->select('COUNT(`document_uid`)')
        ->from($query->qn('inventory_tasks_mastak', 'itm'))
        ->innerJoin(
            $query->qn('inventory_tasks_mastak_category', 'itmc') . ' ON '
            . $query->qn('itmc.category_uid') . ' = ' . $query->qn('itm.category_uid')
        )
        ->innerJoin(
            $query->qn('inventory_users_mastak_category', 'iumc')
            . ' ON ' . $query->qn('iumc.category_uid') . ' = ' . $query->qn('itm.category_uid')
            . ' AND ' . $query->qn('iumc.user_uid') . ' = ' . Visitor::getUID()
        )
        ->where(
            [
                $query->qn('status') . ' = ' . Inventory::TASK_DONT_IN_WORK,
                $query->qn('lifetime') . ' > ' . $query->q(time())
            ]
        );

    if (!empty($warehouseId)) {
        $query->where($query->qn('itm.codeWarehouse') . ' = ' . $warehouseId);
    }

    return db_result(db_query($query));
}

/**
 * Поиск в товара через поисковой запрос
 */
function inventoryInvalidSearch()
{
    $result = [];
    if (!empty($_POST['search'])) {
        $search = trim($_POST['search']);
        $type = !empty($_POST['type']) ? $_POST['type'] : 'invalid';

        $proc = new SiteSearchPreprocess();
        $prodIds = $proc->searchProducts($search);

        if (!empty($prodIds)) {
            $productOnWarehouses = getResultSearchMastakWarehouse($prodIds);

            if (!empty($productOnWarehouses) && count($productOnWarehouses) < 15) {
                $result['resultSearch'] = '
            <h2>Таблица результатов. Найдено ' . count($productOnWarehouses) . '</h2>';
                $number = 1;
                foreach ($productOnWarehouses as $product) {
                    if (empty($product['barcode'])) {
                        $product['barcode'] = getBarcodeProduct($product['OKDP'], (string)Visitor::getWarehouseId());
                    }

                    $result['resultSearch'] .= '<table class="info_table table_result_search" border="1">
                    <tr><td colspan="5" class="title_product">' . $product['title'] . '</td></tr>';
                    $preview = CatalogImage::getProdPreview($product['prodId'], null, null);
                    $result['resultSearch'] .= '<tr><td>' . $number . '</td><td>';
                    if (!empty($preview) && !strpos($preview, 'no-image.jpg')) {
                        $result['resultSearch'] .= '<div class="block_foto_task"><img src="https://' . $_SERVER['SERVER_NAME'] . '/' . $preview . '"></div>';
                    }

                    $result['resultSearch'] .= '</td>
                <td>' . $product['barcode'] . '</td></tr>
                <tr>
                <th class="cell_name" colspan="2">Учетное количество - ' . $product['balance'] . '</th>
                <td ><input type="button" class="view_page_' . $type . '_product" 
                data-prod_title="' . $product['title'] . '"
                data-code_product="' . $product['OKDP'] . '" data-prod_id="' . $product['prodId'] . '" 
                data-barcode="' . $product['barcode'] . '" value="Выбрать"></td>
                </tr>';
                    $number++;
                    $result['resultSearch'] .= '</table>';
                }
            } else {
                $result['error'] = '<div><p class="error_text">Найдено ' . count(
                        $productOnWarehouses
                    ) . '. Измените запрос чтобы актуализировать поиск</p></div>';
            }
        }
    }

    exit(json_encode($result));
}

/** Получение блока отправки товара в брак
 * @param string $OKDP
 * @param string $prodId
 * @param string $title
 * @return array
 */
function viewPageInvalidProduct($OKDP = '', $prodId = '', $title = ''): array
{
    $result = [];
    $OKDP = !empty($OKDP) ? $OKDP : trim($_POST['okdp']);
    $title = !empty($title) ? $title : trim($_POST['title']);
    $prodId = !empty($prodId) ? $prodId : (int)trim($_POST['prodId']);
    if (!empty($OKDP) && !empty($prodId)) {
        $preview = CatalogImage::getProdPreview($prodId, null, null, 'card_main_preview');
        $result['defect'] = '<div class="block_transfer_defect"><h2>Отправка товара в брак</h2>';
        if (!empty($preview) && !strpos($preview, 'no-image.jpg')) {
            $result['defect'] .= '<div class="block_foto_defect"><img src="https://' . $_SERVER['SERVER_NAME'] . '/' . $preview . '"></div>';
        }

        $balance = getAmountProduct($OKDP, (string)Visitor::getWarehouseId());
        $result['defect'] .= '<table class="info_table" border="1">
            <caption>Информация о товаре - ' . $title . '</caption>
            <tr>
                <th class="cell_name">Код товара</th>
                <th class="cell_name">Штрихкод</th>
                <th class="cell_name">Количество</th>
            </tr>
            <tr>
            <td>' . $OKDP . '</td>
            <td>' . getBarcodeByOKDP($prodId) . '</td>
            <td>' . $balance . '</td>
            </tr></table>';

        $result['defect'] .= '<p>Количество перенесенного в брак? <input class="number_transfer_defect" type="text" min="0" max="' . explode(' ', $balance)[0] . '"></p>
                <p>Описание дефекта: (без описания в брак нельзя отправить)</p><textarea type="text" rows="6" cols="55" class="description_defect"></textarea>
                <input type="button" class="transfer_invalid_product" data-code_product="' . $OKDP . '" disabled="disabled" value="Переместить в брак (заполните форму)">';
    }

    if (!empty($_POST['okdp']) || !empty($_POST['prodId'])) {
        exit(json_encode($result));
    }
    return $result;
}

/** Получение штрихкода по prodId
 * @param int $prodId
 * @return bool|mixed
 */
function getBarcodeByOKDP(int $prodId)
{
    $query = DbHelper::getQuery();
    $query->select($query->qn('b.barcode'))
        ->from($query->qn('barcodes', 'b'))
        ->where($query->qn('b.prod_id') . '=' . $query->q($prodId))
        ->limit(1);

    return db_result(db_query($query));
}

/** Получение товаров которые лежат на складе Мастака к которому привязан пользователь
 * @param array $prodIds
 * @return array
 */
function getResultSearchMastakWarehouse(array $prodIds): array
{
    $prodIdsOnWarehouse = [];
    $query = DbHelper::getQuery();
    $query->select(
        [
            $query->qn('OKDP'),
            $query->qn('bar.barcode'),
            $query->qn('prodId'),
            $query->qn('nr.title', 'title'),
        ]
    )
        ->from($query->qn('balances_3_part', 'b3p'))
        ->innerJoin(
            $query->qn('content_type_product', 'ctp') .
            ' ON ' . $query->qn('b3p.OKDP') . ' = ' . $query->qn('ctp.field_okdp_value')
        )
        ->leftJoin(
            $query->qn('barcodes', 'bar')
            . ' ON ' . $query->qn('ctp.field_id_value') . '=' . $query->qn('bar.prod_id')
        )
        ->innerJoin(
            $query->qn('node_revisions', 'nr')
            . ' ON ' . $query->qn('nr.nid') . '=' . $query->qn('ctp.nid')
        )
        ->where($query->qn('prodId') . ' IN(' . implode(',', $prodIds) . ')');

    $result = db_query($query);
    $codeWarehouse = Visitor::getWarehouseId();
    while ($item = db_fetch_array($result)) {
        $item['balance'] = getAmountProduct($item['OKDP'], $codeWarehouse);
        $prodIdsOnWarehouse[] = $item;
    }

    return $prodIdsOnWarehouse;
}

/** Запрос для получения категорий которые видны пользователю
 * @param $query
 */
function getQueryCategoryInfoByUser(&$query)
{
    $query->select(
        [
            $query->qn('itmc.category_name'),
            $query->qn('iumc.category_uid'),
        ]
    )
        ->from($query->qn('inventory_users_mastak_category', 'iumc'))
        ->innerJoin(
            $query->qn('inventory_tasks_mastak_category', 'itmc') . ' ON '
            . $query->qn('itmc.category_uid') . ' = ' . $query->qn('iumc.category_uid')
        )
        ->where($query->qn('iumc.user_uid') . ' = ' . Visitor::getUID());
}

/**
 * Получение html блока категорий для текущих задач или отправка пользователя в незавершенную задачу
 */
function getInventoryCategoryInfo()
{
    if (Inventory::getCountTaskUserInWork(Visitor::getWarehouseId())) {
        transferTaskInWork(Inventory::getInfoTaskUserInWork(Visitor::getWarehouseId()));
    }

    $categoryInfo = [];
    $result = [];
    $query = DbHelper::getQuery();
    getQueryCategoryInfoByUser($query);

    $res = db_query($query);

    while ($row = db_fetch_object($res)) {
        $categoryInfo[$row->category_uid] = $row->category_name;
    }
    if (!empty($categoryInfo)) {
        $result['form'] = '<div class="block_category_inventory">';
        $number = 1;
        foreach ($categoryInfo as $category_uid => $category_name) {
            $countTask = getCountTaskByCategory($category_uid);
            $isCatWork = !empty($countTask) ? 'catWithWork' : 'catNotWork';
            $result['form'] .= '<div class="block_category_info">
                        <input type="text" class="cat_info_input cat_number ' . $isCatWork . '"
                               value="' . $number . '" disabled="disabled">
                        <input type="text" class="cat_info_input cat_name ' . $isCatWork . '"
                               value="' . $category_name . '" disabled="disabled">';
            if (!empty($countTask)) {
                $result['form'] .= '<input type="button" class="cat_info_input category_submit ' . $isCatWork . '"
                         data-category-uid="' . $category_uid . '" value="Выбрать">';
            }
            $result['form'] .= '</div>';
            $number++;
        }
        $result['form'] .= '<div class="block_result_search_task"></div></div>';
    }

    exit(json_encode($result));
}

/**
 * Получение формы категории по смене категории в задаче
 */
function getFormChangeCategory()
{
    $result = '';
    if (!empty($_POST['documentUid'])) {
        $documentUid = $_POST['documentUid'];

        $query = DbHelper::getQuery();
        getQueryCategoryInfoByUser($query);
        $oldCategoryUid = Inventory::getCategoryUidByDocumentUid($documentUid);

        $res = db_query($query);
        while ($row = db_fetch_object($res)) {
            $categoryInfo[$row->category_uid] = $row->category_name;
        }
        if (!empty($categoryInfo)) {
            $result = '<div class="block_category_inventory">
            <input type="button" class="btn_back back_cancel_form" data-document_uid = "' . $documentUid . '" value="Назад">
            <p>Выберите пожалуйста, новую категорию к которой принадлежит товар:</p>';
            $number = 1;
            foreach ($categoryInfo as $category_uid => $category_name) {
                if ($oldCategoryUid == $category_uid) {
                    continue;
                }
                $result .= '<div class="block_category_info">
                        <input type="text" class="cat_info_input cat_number"
                               value="' . $number . '" disabled="disabled">
                        <input type="text" class="cat_info_input cat_name"
                               value="' . $category_name . '" disabled="disabled">
                        <input type="button" class="cat_info_input category_change_submit"
                         data-category_uid="' . $category_uid . '" data-document_uid = "' . $documentUid . '" value="Выбрать">
                         </div>';
                $number++;
            }
            $result .= '<div class="block_result_search_task"></div></div>';
        }
    }
    exit(json_encode($result));
}

/**
 * Изменение категории в задаче
 */
function changeCategory()
{
    $result = false;
    if (!empty($_POST['documentUid']) && !empty($_POST['categoryUid'])) {
        $documentUid = trim($_POST['documentUid']);
        $newCategoryUid = trim($_POST['categoryUid']);

        $query = DbHelper::getQuery();
        $query->select(
            [
                $query->qn('itm.category_uid', 'category_uid'),
                $query->qn('itm.OKDP', 'OKDP')
            ]
        )
            ->from($query->qn('inventory_tasks_mastak', 'itm'))
            ->where($query->qn('itm.document_uid') . ' = ' . $query->q($documentUid))
            ->limit(1);

        $res = db_query($query);

        while ($row = db_fetch_array($res)) {
            $oldCategoryUid = $row['category_uid'];
            $okdp = $row['OKDP'];
        }

        if (isset($okdp) && isset($oldCategoryUid)) {
            if (sendInfoByChangeCategory($okdp, $oldCategoryUid, $newCategoryUid)) {
                $result = assigningDefaultTask($documentUid) && updateCategoryInTask($documentUid, $newCategoryUid);
            }
        }
    }
    exit($result);
}

/** Получаем количество задач по категории не в работе
 * @param string $categoryUid
 * @return bool|mixed
 */
function getCountTaskByCategory(string $categoryUid)
{
    $query = DbHelper::getQuery();
    $query->select('COUNT(' . $query->qn('itm.document_uid') . ')')
        ->from($query->qn('inventory_tasks_mastak', 'itm'))
        ->where(
            [
                $query->qn('itm.category_uid') . ' = ' . $query->q($categoryUid),
                $query->qn('itm.lifetime') . ' > ' . $query->q(time()),
                $query->qn('itm.status') . ' = ' . Inventory::TASK_DONT_IN_WORK
            ]
        );

    return db_result(db_query($query));
}

function getInventoryTasksByCategory()
{
    $tasks = [];
    $result = [];
    if (!empty($_POST['categoryUid'])) {
        $categoryUid = $_POST['categoryUid'];

        $query = DbHelper::getQuery();
        $query->select(
            [
                $query->qn('itm.document_uid', 'document_uid'),
                $query->qn('itm.number', 'number_document'),
                $query->qn('itm.lifetime', 'lifetime'),
                $query->qn('itm.category_uid', 'category_uid'),
                $query->qn('itm.codeWarehouse', 'codeWarehouse'),
                $query->qn('itmc.category_name', 'title'),
                $query->qn('t3L.Nam', 'nameWarehouse'),
            ]
        )
            ->from($query->qn('inventory_tasks_mastak', 'itm'))
            ->leftJoin(
                $query->qn('inventory_tasks_mastak_category', 'itmc') . ' ON '
                . $query->qn('itmc.category_uid') . ' = ' . $query->qn('itm.category_uid')
            )
            ->leftJoin(
                $query->qn('TablePostav3L', 't3L') . ' ON '
                . $query->qn('t3L.mastak_id') . ' = ' . $query->qn('itm.codeWarehouse')
            )
            ->where(
                [
                    $query->qn('itm.category_uid') . ' = ' . $query->q($categoryUid),
                    $query->qn('itm.codeWarehouse') . ' = ' . Visitor::getWarehouseId(),
                    $query->qn('itm.lifetime') . ' > ' . $query->q(time()),
                    $query->qn('itm.status') . ' = ' . Inventory::TASK_DONT_IN_WORK
                ]
            );

        $res = db_query($query);
        $time = time();
        while ($row = db_fetch_array($res)) {
            $row['lifetime'] = date("H:i:s", mktime(0, 0, $row['lifetime'] - $time));
            if (empty($row['title'])) {
                $row['title'] = getCategoryFrom1C($row['category_uid'], $row['codeWarehouse']);
            }
            $tasks[$row['number_document']] = $row;
        }

        if (!empty($tasks)) {
            $result['tasks'] = '<div><input type="button" class="btn_back back_category" value="Назад"></div>
            <h3>Таблица заданий</h3>';
            foreach ($tasks as $number => $task) {
                $result['tasks'] .= '<table class="info_table table_task"><tr>
                <th class="cell_name">№</th>
                <th class="cell_name">Время на исполнение</th>
                <th class="cell_name">Категория</th>
            </tr>
            <tr>
                <td>' . $number . '</td>
                                <td>' . $task['lifetime'] . '</td>
                <td>' . $task['title'] . '</td>
                </tr>
                <tr>
                <th class="cell_name" colspan="3">Действие</th>
                </tr>
                <tr>
                <td colspan="3"><input type="button" class="take_to_work" data-document_uid="' . $task['document_uid'] . '" value="Взять в работу"></td>
                </tr>';
                $result['tasks'] .= '</table>';
            }
        } else {
            $result['error'] = '<p>В данной категории нету для вас задач</p>';
        }
        exit(json_encode($result));
    }
}

/**
 *  Отмена взятия задачи в работу
 */
function transferCancelTask()
{
    $result = false;
    if (!empty($_POST['documentUid'])) {
        $documentUid = (string)$_POST['documentUid'];
        $result = assigningDefaultTask($documentUid);

        global $user;
        $taskInfo = db_fetch_object(
            db_query("SELECT OKDP, number FROM {inventory_tasks_mastak} WHERE document_uid = '%s'", $documentUid)
        );

        $body = '<p>Отменил: ' . $user->name . '</p>';
        $body .= '<p>Причина: ' . $_POST['description'] . '</p>';
        $body .= '<p>УИД документа: ' . $documentUid . '</p>';
        $body .= '<p>ОКДП товара: ' . $taskInfo->OKDP . '</p>';
        $body .= '<p>Номер задачи ' . $taskInfo->number . '</p>';

        $mailsResponsibleOnWarehouse = explode(',', variable_get('mail_warehouseId_' . Visitor::getWarehouseId(), ''));

        if (!empty($mailsResponsibleOnWarehouse)) {
            $mailer = new Mailer();
            foreach ($mailsResponsibleOnWarehouse as $mail) {
                $mailer->sendMail(
                    ['admin@7745.by', 'Инвентаризация Мастака'],
                    [$mail],
                    'Автоматическое информирование',
                    $body
                );
            }
        }
    }
    exit(json_encode($result));
}

/**
 * Переход задачи в работу
 * @param string $documentUid
 */
function transferTaskInWork($documentUid = '')
{
    $result = [];
    if (!empty($_POST['documentUid']) || !empty($documentUid)) {
        $documentUid = !empty($documentUid) ? $documentUid : $_POST['documentUid'];

        $query = DbHelper::getQuery();
        $query->select(
            [
                $query->qn('itm.OKDP', 'OKDP'),
                $query->qn('itm.barcode', 'barcode'),
                $query->qn('itm.document_uid', 'document_uid'),
                $query->qn('itm.codeWarehouse', 'codeWarehouse'),
                $query->qn('ctp.field_id_value', 'prodId'),
                $query->qn('nr.title', 'title'),
            ]
        )
            ->from($query->qn('inventory_tasks_mastak', 'itm'))
            ->leftJoin(
                $query->qn('content_type_product', 'ctp') .
                ' ON ' . $query->qn('itm.OKDP') . ' = ' . $query->qn('ctp.field_okdp_value')
            )
            ->leftJoin(
                $query->qn('node_revisions', 'nr')
                . ' ON ' . $query->qn('nr.nid') . '=' . $query->qn('ctp.nid')
            )
            ->where(
                [
                    $query->qn('itm.document_uid') . ' = ' . $query->q($documentUid),
                ]
            );
        if (!empty($_POST['documentUid'])) {
            $query->where($query->qn('itm.status') . ' = ' . Inventory::TASK_DONT_IN_WORK);
        }

        $product = db_fetch_array(db_query($query));

        if (!empty($product)) {
            $result['form'] = '<div class="block_task_in_work"><input type="button" class="btn_back cancel_task_form"
            data-document_uid="' . $documentUid . '" value="Отмена задания">';
            $product['quantity'] = getAmountProduct($product['OKDP'], $product['codeWarehouse']);

            if (!empty($product['prodId'])) {
                $preview = CatalogImage::getProdPreview($product['prodId'], null, null);
                if (!empty($preview) && !strpos($preview, 'no-image.jpg')) {
                    $result['form'] .= '<div class="block_foto_task"><img src="https://' . $_SERVER['SERVER_NAME'] . '/' . $preview . '"></div>';
                }
            }
            if (empty($product['title'])) {
                watchdog('inventory task', 'По документу ' . $documentUid . ' не найден товар');
                $productInfo = getInfoProductByOKDP($product['OKDP']);
                $product['title'] = $productInfo->{'Наименование'};
            }

            $result['form'] .= '<table class="info_table" border="1" align="center">
            <caption>Таблица задания - ' . $product['title'] . '</caption>
            <tr>
                <th class="cell_name">Штрихкод</th>
                <th class="cell_name">Учетное количество</th>
            </tr>';
            $result['form'] .= '<tr>
                <td>' . $product['barcode'] . '</td>
                <td>' . $product['quantity'] . '</td>
                </tr>
                <tr>
                <th class="cell_name">Фактическое количество</th>
                <th class="cell_name">Действие</th>
                </tr>
                <tr>
                <td><input type="text" class="actual_quantity"></td>
                <td><input type="button" class="send_data" data-document_uid="' . $product['document_uid'] . '" data-code-warehouse="' . $product['codeWarehouse'] . '" value="Отправить"></td>
                </tr>';
            $result['form'] .= '</table>';
            assigningTaskToUser(Visitor::getUID(), $documentUid);
        } else {
            watchdog('inventory task', 'По документу ' . $documentUid . ' не найден товар');
            $result['error'] = 'Данный товар не найден в базе или задача недоступна';
        }
    }
    exit(json_encode($result));
}

/**
 * Отправка данных о количестве товара при инвентаризации в 1С
 */
function inventorySendDocument()
{
    if (!empty($_POST['documentUid']) && isset($_POST['quantity']) && !empty($_POST['codeWarehouse'])) {
        $result['answer'] = sendQuantityResultTask(
            (float)$_POST['quantity'],
            $_POST['documentUid'],
            (int)$_POST['codeWarehouse']
        );
    }
    if (empty($result['answer'])) {
        watchdog(
            'inventorySendTask',
            'По документу ' . $_POST['documentUid'] . ' с количеством товара ' . $_POST['quantity'] .
            ' склада ' . $_POST['codeWarehouse'] . ' ошибка отправки данных'
        );
        $result['error'] = 'Ошибка в передаче данных. Попробуйте через 5 минут';
    }

    exit(json_encode($result));
}

/**
 * Отправка данных о бракованном товаре в 1С
 */
function transferGoodsDefect()
{
    if (!empty($_POST['okdp']) && isset($_POST['quantity']) && !empty($_POST['description'])) {
        $result['answer'] = sendDataForTransferGoodsDefect(
            $_POST['okdp'],
            (float)$_POST['quantity'],
            $_POST['description']
        );
    }

    if (empty($result['answer'])) {
        $result['error'] = 'Ошибка в передаче данных';
    }

    exit(json_encode($result));
}

/**
 *  Отправка данных товара отсканированного через камеру
 */
function inventorySendDocumentByCamera()
{
    if (!empty($_POST['okdp'] && isset($_POST['quantity']))) {
        $result['answer'] = sendDocumentByCamera((float)$_POST['quantity'], $_POST['okdp']);
    }
    if (empty($result['answer'])) {
        $result['error'] = 'Ошибка в передаче данных';
    }

    exit(json_encode($result));
}

/** Получение названия категории если ее нету в БД
 * @param string $categoryUid
 * @param int $codeWarehouse
 * @return bool|string
 */
function getCategoryFrom1C(string $categoryUid, int $codeWarehouse)
{
    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $ch = curl_init(URL_GET_CATEGORY_INFO);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'category_uid' => $categoryUid,
                'code_warehouse' => $codeWarehouse,
            ]
        )
    );
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,

        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);
    return $result->{'Наименование'};
}

/** Получение информации по товару от 1с по штрихкоду
 * @param string $barcode
 * @param string $codeWarehouse
 * @return bool|string
 */
function getInfoProductByBarcode(string $barcode, string $codeWarehouse)
{
    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $ch = curl_init(URL_GET_INFO_PRODUCT_BY_BARCODE);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'product_barcode' => $barcode,
                'codeWarehouse' => $codeWarehouse,
            ]
        )
    );
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,

        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);

    return $result->{"Количество"};
}

/** Получение информации по товару от 1с по ОКДП
 * @param string $OKDP
 * @return bool|string
 */
function getInfoProductByOKDP(string $OKDP)
{
    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $ch = curl_init(URL_GET_INFO_PRODUCT_BY_OKDP);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'product_okdp' => $OKDP
            ]
        )
    );
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,

        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);

    return $result;
}

/** Получение актуального количества товара на складе
 * @param string $OKDP
 * @param string $codeWarehouse
 * @return bool|string
 */
function getAmountProduct(string $OKDP, string $codeWarehouse)
{
    $result = getInfoProduct($OKDP, $codeWarehouse);
    return $result->{"Количество"} . ' ' . $result->{"ЕдИзм"};
}

/** Получаем ШК от 1С по ОКДП
 * @param string $OKDP
 * @param string $codeWarehouse
 */
function getBarcodeProduct(string $OKDP, string $codeWarehouse)
{
    $result = getInfoProduct($OKDP, $codeWarehouse);
    return $result->{"ШтрихКод"};
}

/** Проверка проведения документа в 1С
 * @param string $documentUid
 * @return mixed
 */
function checkDocumentIn1C(string $documentUid)
{
    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $ch = curl_init(URL_CHECKING_TASK_COMPLETION);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'documentUid' => $documentUid
            ]
        )
    );

    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,
        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);
    return $result->{"Проведен"};
}

/** Получаем информацию о товаре от 1С
 * @param string $OKDP
 * @param string $codeWarehouse
 * @return mixed
 */
function getInfoProduct(string $OKDP, string $codeWarehouse)
{
    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $ch = curl_init(URL_GET_AMOUNT_PRODUCT);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'product_okdp' => $OKDP,
                'codeWarehouse' => $codeWarehouse,
            ]
        )
    );
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,

        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);
    return $result;
}

/** Получение запроса для поиска по ШК
 * @param $query
 * @param $barcode
 * @param string $OKDP
 */
function getQueryForSearchBarcode(&$query, $barcode, string $OKDP = '')
{
    $query->select(
        [
            $query->qn('ctp.field_id_value', 'prodId'),
            $query->qn('ctp.field_okdp_value', 'OKDP'),
            $query->qn('nr.title', 'title'),
        ]
    )
        ->from($query->qn('content_type_product', 'ctp'))
        ->leftJoin(
            $query->qn('barcodes', 'bar')
            . ' ON ' . $query->qn('ctp.field_id_value') . '=' . $query->qn('bar.prod_id')
        )
        ->innerJoin(
            $query->qn('node_revisions', 'nr')
            . ' ON ' . $query->qn('nr.nid') . ' = ' . $query->qn('ctp.nid')
        )
        ->limit(1);

    if (!empty($OKDP)) {
        $query->where($query->qn('ctp.field_okdp_value') . '=' . $query->q($OKDP));
    } else {
        $query->where($query->qn('bar.barcode') . '=' . $query->q($barcode));
    }
}

/**
 *  Поиск по ШК
 */
function inventorySearchBarcode()
{
    $result = [];
    if (!empty($_POST['barcode']) || !empty($_POST['okdp'])) {
        $barcode = $_POST['barcode'];
        $OKDP = (string)trim($_POST['okdp']);

        $query = DbHelper::getQuery();
        getQueryForSearchBarcode($query, $barcode, $OKDP);

        if (empty($barcode)) {
            $barcode = getBarcodeProduct($OKDP, (string)Visitor::getWarehouseId());
        }

        $product = db_fetch_array(db_query($query));
        if (!empty($product)) {
            $preview = CatalogImage::getProdPreview($product['prodId'], null, null);
            if (!empty($preview) && !strpos($preview, 'no-image.jpg')) {
                $result['product'] .= '<div class="block_foto_task"><img src="https://' . $_SERVER['SERVER_NAME'] . '/' . $preview . '"></div>';
            }

            $result['product'] .= '<table class="info_table" border="1" align="center">
            <caption>Найденный ТМЦ - ' . $product['title'] . '</caption>
            <tr>
                <th class="cell_name">Штрихкод</th>
                <th class="cell_name">Учетное количество</th>
            </tr>
            <tr>
                <td>' . $barcode . '</td>
                <td>' . getAmountProduct($product['OKDP'], (string)Visitor::getWarehouseId()) . '</td>
            </tr>
            <tr>
                <th class="cell_name">Фактическое количество</th>
                <th class="cell_name">Действие</th>
            </tr>';
            $result['product'] .= '<tr>
                <td><input type="text" class="actual_quantity"></td>
                <td><input type="button" class="send_data_by_camera" data-code-product="' . $product['OKDP'] . '" value="Отправить"></td>
                </tr>';
            $result['product'] .= '</table>';
        }
    }

    exit(json_encode($result));
}

/**
 *  Поиск товара в блоке Брака Инвентаризации по ШК через камеру
 */
function inventorySearchBarcodeDefect()
{
    $result = null;
    if (!empty($_POST['barcode'])) {
        $barcode = (int)$_POST['barcode'];
        $query = DbHelper::getQuery();
        getQueryForSearchBarcode($query, (int)$barcode);
        $product = db_fetch_array(db_query($query));
        if (!empty($product)) {
            $result = viewPageInvalidProduct($product['OKDP'], $product['prodId'], $product['title']);
        }
    }
    exit(json_encode($result));
}

/** Закрепление задачи за пользователем
 * @param $userUid
 * @param string $documentUid
 */
function assigningTaskToUser($userUid, string $documentUid)
{
    db_query(
        "UPDATE {inventory_tasks_mastak} SET user_uid = %d , status = " . Inventory::TASK_IN_WORK . " WHERE document_uid = '%s'",
        $userUid,
        $documentUid
    );
}

/** Установление статуса задачи - Не в работе
 * @param string $documentUid
 * @return bool|false|mysqli_result|resource
 */
function assigningDefaultTask(string $documentUid)
{
    return db_query(
        "UPDATE {inventory_tasks_mastak} SET status = " . Inventory::TASK_DONT_IN_WORK . " , user_uid = null WHERE document_uid = '%s'",
        $documentUid
    );
}

/** Изменение категории в задаче
 * @param string $documentUid
 * @param string $newCategoryUid
 * @return bool|false|mysqli_result|resource
 */
function updateCategoryInTask(string $documentUid, string $newCategoryUid)
{
    return db_query(
        "UPDATE {inventory_tasks_mastak} SET category_uid = '%s' WHERE document_uid = '%s'",
        $newCategoryUid,
        $documentUid
    );
}

/** Установление статуса задачи - Завершена
 * @param string $documentUid
 * @return bool|false|mysqli_result|resource
 */
function completionOfTask(string $documentUid)
{
    return db_query(
        "UPDATE {inventory_tasks_mastak} SET status = " . Inventory::TASK_END . " WHERE document_uid = '%s'",
        $documentUid
    );
}

/**
 * Создаём форму настроек инвентаризации
 *
 * @return mixed  HTML-форма настроек
 */
function inventorySettings()
{
    $form = [];

    // Общие настройки
    $form['view'] = [
        '#type' => 'fieldset',
        '#title' => t('Пароли и логины'),
        '#collapsible' => true,
        '#collapsed' => true,
    ];

    $form['view']['login_inventory_7745'] = [
        '#type' => 'textfield',
        '#title' => 'Логин инвентаризации сайта',
        '#default_value' => variable_get('login_inventory_7745', null),
        '#description' => 'Укажите логин сайта для обмена данными по инвентаризации с 1С',
    ];
    $form['view']['password_inventory_7745'] = [
        '#type' => 'textfield',
        '#title' => 'Пароль инвентаризации сайта',
        '#default_value' => variable_get('password_inventory_7745', null),
        '#description' => 'Укажите пароль сайта для обмена данными по инвентаризации с 1С',
    ];

    $form['view']['login_inventory_1c'] = [
        '#type' => 'textfield',
        '#title' => 'Логин инвентаризации 1С',
        '#default_value' => variable_get('login_inventory_1c', null),
        '#description' => 'Укажите логин 1С для обмена данными по инвентаризации',
    ];

    $form['view']['password_inventory_1с'] = [
        '#type' => 'textfield',
        '#title' => 'Пароль инвентаризации 1С',
        '#default_value' => variable_get('password_inventory_1с', null),
        '#description' => 'Укажите пароль 1С для обмена данными по инвентаризации',
    ];

    return system_settings_form($form);
}

/** Отправка данных о браке
 * @param string $OKDP
 * @param float $quantity
 * @param string $description
 * @return bool|string
 */
function sendDataForTransferGoodsDefect(string $OKDP, float $quantity, string $description)
{
    global $user;

    if (empty($user->warehouse_id)) {
        $user->warehouse_id = Visitor::getWarehouseId();
    }

    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $ch = curl_init(URL_SEND_INFO_PRODUCT_DEFECT);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'product_okdp' => $OKDP,
                'quantity' => $quantity,
                'nameUser' => $user->name,
                'nameId' => $user->uid,
                'codeWarehouse' => (string)$user->warehouse_id,
                'defect' => $description,
            ]
        )
    );
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,

        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);

    return $result->{"СтатусРасшифровка"};
}

/** Отправка данных о смене категории в задаче
 * @param string $OKDP
 * @param string $oldCategoryUid
 * @param string $newCategoryUid
 * @return bool|string
 */
function sendInfoByChangeCategory(string $OKDP, string $oldCategoryUid, string $newCategoryUid)
{
    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $ch = curl_init(URL_CHANGE_CATEGORY);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'product_okdp' => $OKDP,
                'category_uid_old' => $oldCategoryUid,
                'category_uid' => $newCategoryUid,
            ]
        )
    );
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,

        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);

    return !empty($result) && $result->{"Статус"} == 'Акей';
}

/** Отправка данных в 1С c данными по задаче
 * @param float $quantity
 * @param string $documentUid
 * @param int $codeWarehouse
 * @param string $userName
 * @param string $uid
 * @param bool $repeat
 * @return bool|string
 */
function sendQuantityResultTask(
    float $quantity,
    string $documentUid,
    int $codeWarehouse,
    string $userName = '',
    string $uid = '',
    bool $repeat = false
) {
    global $user;

    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $userName = !empty($userName) ? $userName : $user->name;
    $uid = !empty($uid) ? $uid : $user->uid;

    $ch = curl_init(URL_SEND_DOCUMENT_BY_TASK);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'documentUid' => $documentUid,
                'codeWarehouse' => $codeWarehouse,
                'quantity' => $quantity,
                'nameUser' => $userName,
                'nameId' => $uid
            ]
        )
    );
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,

        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);

    if (empty($result) || $result->{"Статус"} != 'Акей') {
        if (!$repeat) {
            Inventory::setTaskInTurn($documentUid, $codeWarehouse, $quantity, $user->name, $user->uid);
            $result->{"СтатусРасшифровка"} = 'При отправке данных возникла ошибка. Данные будут повторно отправлены через 5 минут';
            watchdog(
                'inventorySendTask',
                'По документу ' . $documentUid . ' с количеством товара ' . $quantity .
                ' склада ' . $codeWarehouse . ' ошибка отправки данных'
            );
        }
    }
    completionOfTask($documentUid);

    if ($repeat) {
        return $result->{"Статус"} == 'Акей';
    }

    return $result->{"СтатусРасшифровка"};
}

/** Отправка данных товара отсканированного через камеру
 * @param float $quantity
 * @param string $OKDP
 * @return bool|string
 */
function sendDocumentByCamera(float $quantity, string $OKDP)
{
    global $user;

    if (empty($user->warehouse_id)) {
        $user->warehouse_id = Visitor::getWarehouseId();
    }

    $login = variable_get('login_inventory_1c', null);
    $pass = variable_get('password_inventory_1c', null);

    $ch = curl_init(URL_SEND_DOCUMENT_BY_CAMERA);
    curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "POST");
    curl_setopt(
        $ch,
        CURLOPT_POSTFIELDS,
        json_encode(
            [
                'product_okdp' => $OKDP,
                'quantity' => $quantity,
                'nameUser' => $user->name,
                'nameId' => $user->uid,
                'codeWarehouse' => (string)$user->warehouse_id
            ]
        )
    );
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_setopt(
        $ch,
        CURLOPT_HTTPHEADER,
        array(
            'Content-Type: application/json;charset=UTF-8',
            'Accept: application/json',
            'Login:' . $login,
            'Password:' . $pass,

        )
    );

    $result = json_decode(curl_exec($ch));
    curl_close($ch);

    return $result->{"СтатусРасшифровка"};
}

/**
 * Проверка допуска к работе с сайтом
 * @return bool
 */
function checkAuthorization()
{
    $login = variable_get('login_inventory_7745', null);
    $pass = variable_get('password_inventory_7745', null);

    if (empty($_SERVER['PHP_AUTH_USER'])) {
        // допустим, на текущих настройках сервера нужно пробросить эти заголовки,
        // т.е. это нужно для работы функционала при запуске php не как SAPI (mod_php)
        list(
            $_SERVER['PHP_AUTH_USER'], $_SERVER['PHP_AUTH_PW']
            ) =
            explode(':', base64_decode(substr($_SERVER['HTTP_AUTHORIZATION'], 6)));
    }

    if (empty($_SERVER['PHP_AUTH_USER'])) {
        header('WWW-Authenticate: Basic realm="SOAP"');
        header('HTTP/1.0 401 Unauthorized');
        echo 'Для использования веб-сервиса требуется авторизация';
        exit;
    } elseif (
        !array_key_exists('PHP_AUTH_USER', $_SERVER) || ($_SERVER['PHP_AUTH_USER'] !== $login)
        || !array_key_exists('PHP_AUTH_PW', $_SERVER) || ($_SERVER['PHP_AUTH_PW'] !== $pass)
    ) {
        header('HTTP/1.0 401 Unauthorized');
        echo "Неправильная пара логин/пароль.";
        die();
    }
    return true;
}