/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License
 */

package Utils.Validation;

import static com.intellij.openapi.ui.cellvalidators.ValidatingTableCellRendererWrapper.CELL_VALIDATION_PROPERTY;

import com.intellij.openapi.Disposable;
import com.intellij.openapi.ui.ComponentValidator;
import com.intellij.openapi.ui.ValidationInfo;
import com.intellij.ui.JBColor;
import java.awt.Component;
import java.util.function.Supplier;
import javax.swing.BorderFactory;
import javax.swing.JComponent;
import javax.swing.JTable;
import javax.swing.table.TableModel;

public class UIValidator {

  public static final String ILLEGAL_TEXT_PATTERN = "[{|}]";

  public static ComponentValidator createTableValidator(Disposable disposable, JTable table,
      Supplier<? extends ValidationInfo> validationMethod) {
    table.getModel().addTableModelListener(
        e -> ComponentValidator.getInstance(table)
            .ifPresent(ComponentValidator::revalidate));
    return new ComponentValidator(disposable).withValidator(() -> {
      if (validationMethod.get() != null) {
        if (validationMethod.get().warning) {
          table.setBorder(BorderFactory.createLineBorder(JBColor.YELLOW));
        } else {
          table.setBorder(BorderFactory.createLineBorder(JBColor.RED));
        }
        return validationMethod.get();
      }
      table.setBorder(null);
      return null;
    }).andStartOnFocusLost().installOn(table);
  }

  public static ValidationInfo hasValidationInfo(JComponent component) {
    return component != null ? (ValidationInfo) component
        .getClientProperty(CELL_VALIDATION_PROPERTY) : null;
  }

  private static boolean isTableCellValid(JComponent jComponent) {
    ValidationInfo cellInfo = UIValidator.hasValidationInfo(jComponent);
    if (cellInfo != null && !cellInfo.warning) {
      return false;
    }
    return true;
  }

  public static boolean isTableCellsValid(JTable jTable) {
    TableModel tableModel = jTable.getModel();
    for (int i = 0; i < jTable.getRowCount(); i++) {
      for (int j = 0; j < jTable.getColumnCount(); j++) {
        JComponent component = (JComponent) jTable
            .getCellRenderer(i, j).getTableCellRendererComponent(jTable,
                tableModel.getValueAt(i, j), false, false, i, 0);
        if (!isTableCellValid(component)) {
          return false;
        }
      }
    }
    return true;
  }

  public static boolean isComponentValid(JComponent component) {
    if (!ComponentValidator.getInstance(component).isPresent()) {
      return true;
    }
    ComponentValidator componentValidator = ComponentValidator.getInstance(component).get();
    componentValidator.revalidate();
    return componentValidator.getValidationInfo() == null || componentValidator
        .getValidationInfo().warning;
  }

  public static boolean isTableCellsValidDeep(JTable jTable) {
    TableModel tableModel = jTable.getModel();
    for (int i = 0; i < jTable.getRowCount(); i++) {
      for (int j = 0; j < jTable.getColumnCount(); j++) {
        JComponent component = (JComponent) jTable
            .getCellRenderer(i, j).getTableCellRendererComponent(jTable,
                tableModel.getValueAt(i, j), false, false, i, 0);
        if (!isTableCellValid(component)) {
          return false;
        }
        for (Component childComp : component.getComponents()) {
          if (childComp instanceof JComponent && !isTableCellValid((JComponent) childComp)) {
            return false;
          }
        }
      }
    }
    return true;
  }
}
